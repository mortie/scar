package main

import (
	"bytes"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"bufio"
	"flag"
)

const (
	HDR_TYPE_FILE = iota
	HDR_TYPE_HARDLINK
	HDR_TYPE_SYMLINK
	HDR_TYPE_CHARDEV
	HDR_TYPE_BLOCKDEV
	HDR_TYPE_DIR
	HDR_TYPE_FIFO
	HDR_TYPE_PAX_NEXT
	HDR_TYPE_PAX_GLOBAL
	HDR_TYPE_GNU_PATH
	HDR_TYPE_GNU_LINK_PATH
	HDR_TYPE_UNKNOWN
)

var (
	// Old TAR header
	HDR_PATH      = HdrField{0, 100}
	HDR_MODE      = HdrFieldAfter(HDR_PATH, 8)
	HDR_OWNER_UID = HdrFieldAfter(HDR_MODE, 8)
	HDR_OWNER_GID = HdrFieldAfter(HDR_OWNER_UID, 8)
	HDR_SIZE      = HdrFieldAfter(HDR_OWNER_GID, 12)
	HDR_MTIME     = HdrFieldAfter(HDR_SIZE, 12)
	HDR_CHKSUM    = HdrFieldAfter(HDR_MTIME, 8)
	HDR_TYPE      = HdrFieldAfter(HDR_CHKSUM, 1)
	HDR_LINK_PATH = HdrFieldAfter(HDR_TYPE, 100)

	// UStar extension
	HDR_MAGIC       = HdrFieldAfter(HDR_LINK_PATH, 6)
	HDR_VERSION     = HdrFieldAfter(HDR_MAGIC, 2)
	HDR_OWNER_UNAME = HdrFieldAfter(HDR_VERSION, 32)
	HDR_OWNER_GNAME = HdrFieldAfter(HDR_OWNER_UNAME, 32)
	HDR_DEVMAJOR    = HdrFieldAfter(HDR_OWNER_GNAME, 8)
	HDR_DEVMINOR    = HdrFieldAfter(HDR_DEVMAJOR, 8)
	HDR_PREFIX      = HdrFieldAfter(HDR_DEVMINOR, 155)
)

var CompressAlgos = map[string]CompressAlgo {
	"gzip": {
		Magic: []byte{0x1f, 0x8b},
		NewReader: func(r io.Reader) (io.ReadCloser, error) {
			return gzip.NewReader(r)
		},
		NewWriter: func(w io.Writer) CompressedWriter {
			return gzip.NewWriter(w)
		},
	},
}

type CompressAlgo struct {
	Magic []byte
	NewReader func(io.Reader) (io.ReadCloser, error)
	NewWriter func(io.Writer) CompressedWriter
}

type HdrField struct {
	Offset uint
	Length uint
}

func HdrFieldAfter(other HdrField, size uint) HdrField {
	return HdrField{other.Offset + other.Length, size}
}

type Block = [512]byte

type EntryHeader struct {
	ATime    float64
	GId      int
	GName    []byte
	LinkPath []byte
	MTime    float64
	Path     []byte
	Size     int64
	UId      int
	UName    []byte
	Type     int
	Mode     int
	DevMajor int
	DevMinor int
}

func Log10Ceil(num int) int {
	log := 0
	for num > 0 {
		log += 1
		num /= 10
	}
	return log
}

func ReadAll(r io.Reader, buf []byte) error {
	n, err := r.Read(buf)
	if err != nil {
		return err
	} else if n != len(buf) {
		return errors.New("Short read")
	}

	return nil
}

func WriteAll(w io.Writer, buf []byte) error {
	n, err := w.Write(buf)
	if err != nil {
		return err
	} else if n != len(buf) {
		return errors.New("Short write")
	}

	return nil
}

func RoundBlock(size int64) int64 {
	// I hope Go optimizes this?
	for size%512 != 0 {
		size += 1
	}

	return size
}

func BufIsZero(buf []byte) bool {
	for _, ch := range buf {
		if ch != 0 {
			return false
		}
	}

	return true
}

func ParseType(ch byte) int {
	switch ch {
	case 0:
		return HDR_TYPE_FILE
	case '0':
		return HDR_TYPE_FILE
	case '1':
		return HDR_TYPE_HARDLINK
	case '2':
		return HDR_TYPE_SYMLINK
	case '3':
		return HDR_TYPE_CHARDEV
	case '4':
		return HDR_TYPE_BLOCKDEV
	case '5':
		return HDR_TYPE_DIR
	case '6':
		return HDR_TYPE_FIFO
	case '7':
		return HDR_TYPE_FILE // really contiguous file but we treat it like a normal file
	case 'x':
		return HDR_TYPE_PAX_NEXT
	case 'g':
		return HDR_TYPE_PAX_GLOBAL
	case 'L':
		return HDR_TYPE_GNU_PATH
	case 'K':
		return HDR_TYPE_GNU_LINK_PATH
	default:
		return HDR_TYPE_UNKNOWN
	}
}

func ParseStringField(block *Block, field HdrField) []byte {
	var strlength uint = 0
	for {
		if block[field.Offset+strlength] == 0 {
			break
		}

		strlength += 1
	}

	return block[field.Offset : field.Offset+strlength]
}

func ParsePathField(block *Block, field HdrField) []byte {
	path := ParseStringField(block, field)
	if block[HDR_PREFIX.Offset] == 0 {
		return path
	} else {
		return append(append(ParseStringField(block, HDR_PREFIX), byte('/')), path...)
	}
}

func ParseOctalField(block *Block, field HdrField) int64 {
	length := int64(0)
	for i := uint(0); i < field.Length; i++ {
		ch := block[field.Offset+i]
		if ch == 0 || ch == ' ' {
			break
		}

		length *= 8
		length += int64(ch - '0')
	}

	return length
}

func ParseOctal255Field(block *Block, field HdrField) int64 {
	// GNU will set the first bit high, then treat the rest of the bytes as base 256
	data := block[field.Offset : field.Offset+field.Length]
	if data[0]&0b10000000 != 0 {
		length := int64(data[0] & 0b01111111)
		for i := uint(1); i < field.Length; i++ {
			ch := block[field.Offset+i]
			if ch == 0 {
				break
			}

			length *= 256
			length += int64(data[i])
		}

		return length
	}

	return ParseOctalField(block, field)
}

func ParseHeader(block *Block, next map[string][]byte, global map[string][]byte) (EntryHeader, error) {
	header := EntryHeader{}
	var err error

	// Fields which may use the UStar 'prefix' mechanism
	readPathField := func(name string, field HdrField) []byte {
		if path, ok := next[name]; ok {
			return path
		} else if path, ok := global[name]; ok {
			return path
		} else {
			return ParsePathField(block, field)
		}
	}

	// Fields which are either floats in pax, or octal integers in the header block
	readOctalOrFloatField := func(name string, field HdrField) (float64, error) {
		if val, ok := next[name]; ok {
			return strconv.ParseFloat(string(val), 64)
		} else if val, ok := global[name]; ok {
			return strconv.ParseFloat(string(val), 64)
		} else if field.Length > 0 {
			return float64(ParseOctalField(block, field)), nil
		} else {
			return -1, nil
		}
	}

	// Fields which are base 10 fields in pax, or octal in the header block
	readOctalOrIntField := func(name string, field HdrField) (int, error) {
		if val, ok := next[name]; ok {
			num, err := strconv.ParseInt(string(val), 10, 32)
			if err != nil {
				return -1, err
			}
			return int(num), nil
		} else if val, ok := global[name]; ok {
			num, err := strconv.ParseInt(string(val), 10, 32)
			if err != nil {
				return -1, err
			}
			return int(num), nil
		} else {
			return int(ParseOctalField(block, field)), nil
		}
	}

	// Fields which are base 10 fields in pax, or octal/base255 in the header block
	readOctal255OrIntField := func(name string, field HdrField) (int64, error) {
		if val, ok := next[name]; ok {
			return strconv.ParseInt(string(val), 10, 64)
		} else if val, ok := global[name]; ok {
			return strconv.ParseInt(string(val), 10, 64)
		} else {
			return ParseOctal255Field(block, field), nil
		}
	}

	// Plain old string field
	readStringField := func(name string, field HdrField) []byte {
		if val, ok := next[name]; ok {
			return val
		} else if val, ok := global[name]; ok {
			return val
		} else {
			return ParseStringField(block, field)
		}
	}

	header.ATime, err = readOctalOrFloatField("atime", HdrField{0, 0})
	if err != nil {
		return header, err
	}

	header.GId, err = readOctalOrIntField("gid", HDR_OWNER_GID)
	if err != nil {
		return header, err
	}

	header.GName = readStringField("gname", HDR_OWNER_GNAME)
	header.LinkPath = readPathField("linkpath", HDR_LINK_PATH)

	header.MTime, err = readOctalOrFloatField("mtime", HDR_MTIME)
	if err != nil {
		return header, err
	}

	header.Path = readPathField("path", HDR_PATH)

	header.Size, err = readOctal255OrIntField("size", HDR_SIZE)
	if err != nil {
		return header, err
	}

	header.UId, err = readOctalOrIntField("uid", HDR_OWNER_UID)
	if err != nil {
		return header, err
	}

	header.UName = readStringField("uname", HDR_OWNER_UNAME)

	header.Type = ParseType(block[HDR_TYPE.Offset])
	header.Mode = int(ParseOctalField(block, HDR_MODE))
	header.DevMajor = int(ParseOctalField(block, HDR_DEVMAJOR))
	header.DevMinor = int(ParseOctalField(block, HDR_DEVMINOR))

	return header, nil
}

func ReadFileContent(r io.Reader, size int64) ([]byte, []byte, error) {
	buf := make([]byte, RoundBlock(size))
	err := ReadAll(r, buf)
	if err != nil {
		return nil, nil, err
	}

	return buf[:size], buf, nil
}

func DumpFileContent(r io.Reader, w io.Writer, size int64) error {
	block := Block{}
	for size > 0 {
		err := ReadAll(r, block[:])
		if err != nil {
			return err
		}

		err = WriteAll(w, block[:])
		if err != nil {
			return err
		}

		size -= 512
	}

	return nil
}

func WritePaxSyntax(w io.Writer, content map[string][]byte) error {
	for k, v := range content {
		size := 0
		size += 1 + len(k) + 1 + len(v) + 1 // ' ' key '=' val '\n'

		digitCount := Log10Ceil(size)
		digits := fmt.Sprintf("%d", size+digitCount)
		if len(digits) != digitCount {
			digits = fmt.Sprintf("%d", size+digitCount+1)
		}

		_, err := fmt.Fprintf(w, "%s %s=", digits, k)
		if err != nil {
			return err
		}

		err = WriteAll(w, v)
		if err != nil {
			return err
		}

		err = WriteAll(w, []byte("\n"))
		if err != nil {
			return err
		}
	}

	return nil
}

func ParsePaxSyntax(content []byte) (map[string][]byte, error) {
	pax := map[string][]byte{}

	offset := 0
	for offset < len(content) {
		fieldStart := offset
		fieldLength := 0
		for offset < len(content) && content[offset] != ' ' {
			fieldLength *= 10
			fieldLength += int(content[offset] - '0')
			offset += 1
		}

		offset += 1 // skip space
		if offset >= len(content) {
			return pax, errors.New("Pax header length prefix too long")
		}

		if fieldStart+fieldLength > len(content) {
			return pax, errors.New("Pax header field too long")
		}

		keywordStart := offset
		for offset < len(content) && content[offset] != '=' {
			offset += 1
		}

		keyword := string(content[keywordStart:offset])

		offset += 1 // skip '='
		if offset >= len(content) {
			return pax, errors.New("Pax header keyword too long")
		}

		payloadLength := fieldLength - (offset - fieldStart) - 1
		payload := content[offset : offset+payloadLength]
		offset += payloadLength

		if content[offset] != '\n' {
			return pax, errors.New("Missing newline at the end of field")
		}
		offset += 1

		pax[keyword] = payload
	}

	return pax, nil
}

type TarWriter interface {
	io.Writer
	Flush() error
	Written() int64
}

type BasicTarWriter struct {
	w            io.Writer
	WrittenCount int64
}

func (tw *BasicTarWriter) Write(p []byte) (int, error) {
	n, err := tw.w.Write(p)
	if err != nil {
		return n, err
	}

	tw.WrittenCount += int64(n)
	return n, err
}

func (tw *BasicTarWriter) Written() int64 {
	return tw.WrittenCount
}

func (tw *BasicTarWriter) Flush() error {
	return nil
}

func NewBasicTarWriter(w io.Writer) *BasicTarWriter {
	return &BasicTarWriter{w, 0}
}

type CompressedWriter interface {
	Close() error
	Reset(io.Writer)
	Write([]byte) (int, error)
}

type RecordedFlush struct {
	rawOffset        int64
	compressedOffset int64
}

type CompressedTarWriter struct {
	W          *BasicTarWriter
	Z          CompressedWriter
	rawWritten int64
	Flushes    []RecordedFlush
}

func (tw *CompressedTarWriter) Write(p []byte) (int, error) {
	tw.rawWritten += int64(len(p))
	return tw.Z.Write(p)
}

func (tw *CompressedTarWriter) Written() int64 {
	return tw.W.Written()
}

func (tw *CompressedTarWriter) Flush() error {
	err := tw.Z.Close()
	if err != nil {
		return err
	}

	tw.Flushes = append(tw.Flushes, RecordedFlush{tw.rawWritten, tw.W.Written()})

	tw.Z.Reset(tw.W)
	return nil
}

func (tw *CompressedTarWriter) Close() error {
	return tw.Z.Close()
}

func CreateIndexedTar(r io.Reader, w TarWriter, iw io.Writer, flushThreshold int64) error {
	headerBlock := Block{}

	offset := int64(0)
	next := map[string][]byte{}
	global := map[string][]byte{}
	previousFlush := int64(0)
	fileMetaStart := int64(0)
	indexEntryBuffer := bytes.Buffer{}

	for {
		if w.Written() > previousFlush+flushThreshold {
			w.Flush()
			previousFlush = w.Written()
		}

		err := ReadAll(r, headerBlock[:])
		if err != nil {
			return err
		}

		// Two all-zero blocks in a row is an end marker
		if BufIsZero(headerBlock[:]) {
			err = WriteAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = ReadAll(r, headerBlock[:])
			if err != nil {
				return err
			}

			err = WriteAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			if !BufIsZero(headerBlock[:]) {
				return errors.New("Partial end marker")
			}

			break
		}

		var size int64
		if val, ok := next["size"]; ok {
			size, err = strconv.ParseInt(string(val), 10, 64)
			if err != nil {
				return err
			}
		} else if val, ok := global["size"]; ok {
			size, err = strconv.ParseInt(string(val), 10, 64)
			if err != nil {
				return err
			}
		} else {
			size = ParseOctal255Field(&headerBlock, HDR_SIZE)
		}

		writeIndexEntry := true
		isUnknown := false

		switch ParseType(headerBlock[HDR_TYPE.Offset]) {
		case HDR_TYPE_FILE: fallthrough
		case HDR_TYPE_HARDLINK: fallthrough
		case HDR_TYPE_SYMLINK: fallthrough
		case HDR_TYPE_CHARDEV: fallthrough
		case HDR_TYPE_BLOCKDEV: fallthrough
		case HDR_TYPE_DIR: fallthrough
		case HDR_TYPE_FIFO:
			err := WriteAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = DumpFileContent(r, w, size)
			if err != nil {
				return err
			}

			next = map[string][]byte{}

		case HDR_TYPE_PAX_GLOBAL:
			writeIndexEntry = false

			content, buf, err := ReadFileContent(r, size)
			if err != nil {
				return err
			}

			pax, err := ParsePaxSyntax(content)
			if err != nil {
				return err
			}

			for k, v := range pax {
				global[k] = v
			}

			err = WriteAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = WriteAll(w, buf)
			if err != nil {
				return err
			}

		case HDR_TYPE_PAX_NEXT:
			content, buf, err := ReadFileContent(r, size)
			writeIndexEntry = false

			if err != nil {
				return err
			}

			pax, err := ParsePaxSyntax(content)
			if err != nil {
				return err
			}

			for k, v := range pax {
				next[k] = v
			}

			err = WriteAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = WriteAll(w, buf)
			if err != nil {
				return err
			}

		case HDR_TYPE_GNU_PATH:
			writeIndexEntry = false

			content, buf, err := ReadFileContent(r, size)
			if err != nil {
				return err
			}

			next["path"] = content

			err = WriteAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = WriteAll(w, buf)
			if err != nil {
				return err
			}

		case HDR_TYPE_GNU_LINK_PATH:
			writeIndexEntry = false

			content, buf, err := ReadFileContent(r, size)
			if err != nil {
				return err
			}

			next["linkpath"] = content

			err = WriteAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = WriteAll(w, buf)
			if err != nil {
				return err
			}

		default:
			writeIndexEntry = false
			isUnknown = true

			err := WriteAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = DumpFileContent(r, w, size)
			if err != nil {
				return err
			}

			next = map[string][]byte{}
		}

		if writeIndexEntry {
			indexEntryBuffer.Reset()

			var path []byte
			if val, ok := next["path"]; ok {
				path = val
			} else if val, ok := global["path"]; ok {
				path = val
			} else {
				path = ParsePathField(&headerBlock, HDR_PATH)
			}

			pax := map[string][]byte{}

			for k, v := range global {
				if _, ok := next[k]; !ok {
					pax[k] = v
				}
			}

			pax["scar:offset"] = []byte(strconv.FormatInt(fileMetaStart, 10))
			pax["path"] = path

			indexEntryBuffer.Reset()
			WritePaxSyntax(&indexEntryBuffer, pax)

			indexEntrySize := 1 + indexEntryBuffer.Len() // ' ' entry
			digitCount := Log10Ceil(indexEntrySize)
			digits := fmt.Sprintf("%d", indexEntrySize+digitCount)
			if len(digits) != digitCount {
				digits = fmt.Sprintf("%d", indexEntrySize+digitCount+1)
			}

			_, err = fmt.Fprintf(iw, "%s ", digits)
			if err != nil {
				return err
			}

			err = WriteAll(iw, indexEntryBuffer.Bytes())
			if err != nil {
				return err
			}
		}

		offset += 512 + RoundBlock(size)
		if writeIndexEntry || isUnknown {
			fileMetaStart = offset
		}
	}

	return nil
}

func CreateIndexedCompressedTar(r io.Reader, w io.Writer, thresh int64, algo *CompressAlgo) error {
	indexBuffer := bytes.Buffer{}
	_, err := fmt.Fprintf(&indexBuffer, "SCAR-INDEX\n")
	if err != nil {
		return err
	}

	btw := NewBasicTarWriter(w)
	ctw := CompressedTarWriter{btw, algo.NewWriter(btw), 0, []RecordedFlush{}}
	defer ctw.Close()

	err = CreateIndexedTar(r, &ctw, &indexBuffer, thresh)
	if err != nil {
		return err
	}

	err = ctw.Flush()
	if err != nil {
		return err
	}

	indexStartOffset := ctw.Flushes[len(ctw.Flushes)-1].compressedOffset

	_, err = ctw.Write(indexBuffer.Bytes())
	if err != nil {
		return err
	}

	err = ctw.Flush()
	if err != nil {
		return err
	}

	chunksStartOffset := ctw.Written() + 1

	_, err = fmt.Fprintf(&ctw, "SCAR-CHUNKS\n")
	if err != nil {
		return err
	}

	for _, record := range ctw.Flushes[:len(ctw.Flushes)-2] {
		_, err = fmt.Fprintf(&ctw, "%d %d\n", record.compressedOffset, record.rawOffset)
		if err != nil {
			return err
		}
	}

	err = ctw.Flush()
	if err != nil {
		return err
	}

	_, err = fmt.Fprintf(&ctw, "SCAR-TAIL\n")
	if err != nil {
		return err
	}

	_, err = fmt.Fprintf(&ctw, "%d\n", indexStartOffset)
	if err != nil {
		return err
	}

	_, err = fmt.Fprintf(&ctw, "%d\n", chunksStartOffset)
	if err != nil {
		return err
	}

	return nil
}

func FindTail(r io.ReadSeeker) (int64, int64, *CompressAlgo, error) {
	_, err := r.Seek(-512, io.SeekEnd)
	if err != nil {
		return 0, 0, nil, err
	}

	zTailBuf := [512]byte{}
	zTailBufLength, err := r.Read(zTailBuf[:])
	if err != nil {
		if err != io.EOF || zTailBufLength == 0 {
			return 0, 0, nil, err
		}
	}

	tailBuf := [512]byte{}
	tailMagic := []byte{'S', 'C', 'A', 'R', '-', 'T', 'A', 'I', 'L', '\n'}

	for {
		lastIndex := -1
		var lastAlgo *CompressAlgo = nil
		for _, algo := range CompressAlgos {
			idx := bytes.LastIndex(zTailBuf[:zTailBufLength], algo.Magic)
			if idx > lastIndex {
				lastIndex = idx
				lastAlgo = &algo
			}
		}

		if lastAlgo == nil {
			return 0, 0, nil, errors.New("Couldn't find scar footer at the end of archive.")
		}

		zTailReader := bytes.NewReader(zTailBuf[lastIndex:])
		tailReader, err := lastAlgo.NewReader(zTailReader)
		if err != nil {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			continue
		}

		n, err := tailReader.Read(tailBuf[:])
		if err != nil && err != io.EOF {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			continue
		}

		if n < len(tailMagic) || !bytes.Equal(tailBuf[:len(tailMagic)], tailMagic) {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			continue
		}

		tail := string(tailBuf[len(tailMagic):])
		parts := strings.Split(tail, "\n")

		if len(parts) < 2 {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			continue
		}

		indexOffset, err := strconv.ParseInt(parts[0], 10, 64)
		if err != nil {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			continue
		}

		chunksOffset, err := strconv.ParseInt(parts[0], 10, 64)
		if err != nil {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			continue
		}

		return indexOffset, chunksOffset, lastAlgo, nil
	}
}

func ReadIndexEntry(r *bufio.Reader, tmpbuf *bytes.Buffer) (map[string][]byte, error) {
	digitsBuf := [32]byte{}
	digitsLength := 0

	for {
		ch, err := r.ReadByte()
		if err != nil {
			return nil, err
		}

		if ch == ' ' {
			break
		}

		digitsBuf[digitsLength] = ch
		digitsLength += 1

		if digitsLength >= len(digitsBuf) {
			return nil, errors.New("Entry length too big")
		}
	}

	entryLength, err := strconv.ParseInt(string(digitsBuf[:digitsLength]), 10, 64)
	if err != nil {
		return nil, err
	}

	entryBodyLength := int(entryLength) - digitsLength - 1

	tmpbuf.Reset()
	tmpbuf.Grow(entryBodyLength)
	_, err = io.CopyN(tmpbuf, r, int64(entryBodyLength))
	if err != nil {
		return nil, err
	}

	return ParsePaxSyntax(tmpbuf.Bytes())
}

func ListArchive(r io.ReadSeeker, w io.Writer) error {
	indexOffset, _, algo, err := FindTail(r)
	if err != nil {
		return err
	}

	_, err = r.Seek(indexOffset, io.SeekStart)
	if err != nil {
		return err
	}

	indexReader, err := algo.NewReader(r)
	if err != nil {
		return err
	}
	defer indexReader.Close()

	indexBufReader := bufio.NewReader(indexReader)

	indexMagic := []byte{'S', 'C', 'A', 'R', '-', 'I', 'N', 'D', 'E', 'X', '\n'}
	magicBuf := make([]byte, len(indexMagic))
	n, err := indexReader.Read(magicBuf[:])
	if err != nil && err != io.EOF {
		return err
	} else if n != len(indexMagic) || !bytes.Equal(indexMagic[:], magicBuf[:]) {
		return fmt.Errorf("Invalid index header magic: %v", magicBuf[:n])
	}

	chunksMagic := []byte{'S', 'C', 'A', 'R', '-', 'C', 'H', 'U', 'N', 'K', 'S', '\n'}

	tmpbuf := bytes.NewBuffer(nil)
	for {
		chunksMagicBuf, err := indexBufReader.Peek(len(chunksMagic))
		if err != nil {
			return err
		}

		if bytes.Equal(chunksMagic, chunksMagicBuf) {
			break
		}

		pax, err := ReadIndexEntry(indexBufReader, tmpbuf)
		if err == io.EOF {
			break
		} else if err != nil {
			return err
		}

		fmt.Fprintf(w, "%s\n", pax["path"])
	}

	return nil
}

func main() {
	var err error

	flag.Usage = func() {
		w := flag.CommandLine.Output()
		fmt.Fprintf(w, "Usage:\n")
		fmt.Fprintf(w,
			"  scar [options] add-index\n" +
			"        Create a scar archive from the input file. "+
			"Uses the compression algorithm given by the 'compression' option.\n")
		fmt.Fprintf(w,
			"  scar [options] cat files...\n" +
			"        Read the given files\n")
		fmt.Fprintf(w,
			"  scar [options] subset patterns...\n" +
			"        Output a tarball with files matching the given patterns.\n")
		fmt.Fprintf(w,
			"  scar [options] list\n" +
			"        List arcive contents\n")

		fmt.Fprintf(w, "\nOptions:\n")
		flag.PrintDefaults()

		fmt.Fprintf(w, "\nExamples:\n")
		fmt.Fprintf(w,
			"  tar c dir | scar add-index -o archive.tgz\n" +
			"        Create a scar file\n")
		fmt.Fprintf(w,
			"  scar -i archive.tgz list\n" +
			"        Read the list of files in the archive\n")
		fmt.Fprintf(w,
			"  scar -i archive.tgz cat ./dir/myfile.txt\n" +
			"        Read a file from an archive\n")
		fmt.Fprintf(w,
			"  scar -i archive.tgz subset './dir/a/*' | tar x\n" +
			"        Extract everything in './dir/a'\n")
	}

	inFlag := flag.String("i", "-", "Input file, '-' for stdin.")
	outFlag := flag.String("o", "-", "Output file, '-' for stdout.")
	blocksizeFlag := flag.Int64("blocksize", 1024, "Approximate size of a block, in kiB.")
	compressionFlag := flag.String("compression", "gzip", "Which compression algorithm to use.")
	flag.Parse()

	var inFile *os.File
	if *inFlag == "-" {
		inFile = os.Stdin
	} else {
		inFile, err = os.Open(*inFlag)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: %v\n", *inFlag, err)
			os.Exit(1)
		}
	}
	defer inFile.Close()

	var outFile *os.File
	if *outFlag == "-" {
		outFile = os.Stdout
	} else {
		outFile, err = os.Create(*outFlag)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: %v\n", *inFlag, err)
			os.Exit(1)
		}
	}
	defer outFile.Close()

	blockSize := *blocksizeFlag * 1024

	var algo CompressAlgo
	var ok bool
	if algo, ok = CompressAlgos[*compressionFlag]; !ok {
		fmt.Fprintf(os.Stderr, "Unsupported compression algorithm: '%s'.\n", *compressionFlag)
		fmt.Fprintf(os.Stderr, "Available algorithms: gzip\n")
		os.Exit(1)
	}

	if len(flag.Args()) < 1 {
		flag.Usage()
		os.Exit(1)
	}

	subcommand := flag.Arg(0)
	switch subcommand {
	case "add-index":
		err = CreateIndexedCompressedTar(inFile, outFile, blockSize, &algo)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}

	case "cat":
		fmt.Fprintf(os.Stderr, "Subcommand not implemented: cat\n")
		os.Exit(1)

	case "subset":
		fmt.Fprintf(os.Stderr, "Subcommand not implemented: subset\n")
		os.Exit(1)

	case "list":
		err = ListArchive(inFile, outFile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}

	default:
		fmt.Fprintf(os.Stderr, "Unknown subcommand: %s\n", subcommand)
		flag.Usage()
		os.Exit(1)
	}
}
