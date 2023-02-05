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
		payload := make([]byte, payloadLength)
		copy(payload, content[offset : offset+payloadLength])
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

type SeekPoint struct {
	RawOffset        int64
	CompressedOffset int64
}

type CompressedTarWriter struct {
	W          *BasicTarWriter
	Z          CompressedWriter
	rawWritten int64
	Flushes    []SeekPoint
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

	tw.Flushes = append(tw.Flushes, SeekPoint{tw.rawWritten, tw.W.Written()})

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
	ctw := CompressedTarWriter{btw, algo.NewWriter(btw), 0, []SeekPoint{}}
	defer ctw.Close()

	err = CreateIndexedTar(r, &ctw, &indexBuffer, thresh)
	if err != nil {
		return err
	}

	err = ctw.Flush()
	if err != nil {
		return err
	}

	indexStartOffset := ctw.Flushes[len(ctw.Flushes)-1].CompressedOffset

	_, err = ctw.Write(indexBuffer.Bytes())
	if err != nil {
		return err
	}

	err = ctw.Flush()
	if err != nil {
		return err
	}

	chunksStartOffset := ctw.Flushes[len(ctw.Flushes)-1].CompressedOffset

	_, err = fmt.Fprintf(&ctw, "SCAR-CHUNKS\n")
	if err != nil {
		return err
	}

	for _, record := range ctw.Flushes[:len(ctw.Flushes)-2] {
		_, err = fmt.Fprintf(&ctw, "%d %d\n", record.CompressedOffset, record.RawOffset)
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

		chunksOffset, err := strconv.ParseInt(parts[1], 10, 64)
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

func ReadIndex(r io.ReadSeeker, indexOffset int64, algo *CompressAlgo) ([]map[string][]byte, error) {
	_, err := r.Seek(indexOffset, io.SeekStart)
	if err != nil {
		return nil, err
	}

	indexReader, err := algo.NewReader(r)
	if err != nil {
		return nil, err
	}
	defer indexReader.Close()

	indexBufReader := bufio.NewReader(indexReader)

	indexMagic := []byte("SCAR-INDEX\n")
	magicBuf := make([]byte, len(indexMagic))
	n, err := indexReader.Read(magicBuf[:])
	if err != nil && err != io.EOF {
		return nil, err
	} else if n != len(indexMagic) || !bytes.Equal(indexMagic[:], magicBuf[:]) {
		return nil, fmt.Errorf("Invalid index header magic: %v", magicBuf[:n])
	}

	chunksMagic := []byte("SCAR-CHUNKS\n")
	index := []map[string][]byte{}

	tmpbuf := bytes.NewBuffer(nil)
	for {
		chunksMagicBuf, err := indexBufReader.Peek(len(chunksMagic))
		if err != nil {
			return nil, err
		}

		if bytes.Equal(chunksMagic, chunksMagicBuf) {
			break
		}

		pax, err := ReadIndexEntry(indexBufReader, tmpbuf)
		if err == io.EOF {
			break
		} else if err != nil {
			return nil, err
		}

		index = append(index, pax)
	}

	return index, nil
}

func ReadChunks(r io.ReadSeeker, chunksOffset int64, algo *CompressAlgo) ([]SeekPoint, error) {
	_, err := r.Seek(chunksOffset, io.SeekStart)
	if err != nil {
		return nil, err
	}

	chunksReader, err := algo.NewReader(r)
	if err != nil {
		return nil, err
	}
	defer chunksReader.Close()

	chunks := []SeekPoint{}

	chunksScanner := bufio.NewScanner(chunksReader)

	chunksMagic := []byte("SCAR-CHUNKS")
	if ! chunksScanner.Scan() || !bytes.Equal(chunksScanner.Bytes(), chunksMagic) {
		return nil, errors.New("Missing SCAR-CHUNKS marker")
	}

	tailMagic := []byte("SCAR-TAIL")
	for chunksScanner.Scan() {
		line := chunksScanner.Bytes()
		if bytes.Equal(line, tailMagic) {
			return chunks, nil
		}

		var chunk SeekPoint
		fmt.Sscanf(string(line), "%d %d", &chunk.CompressedOffset, &chunk.RawOffset)
		chunks = append(chunks, chunk)
	}

	return chunks, nil
}

func ListArchive(r io.ReadSeeker, w io.Writer) error {
	indexOffset, _, algo, err := FindTail(r)
	if err != nil {
		return err
	}

	index, err := ReadIndex(r, indexOffset, algo)
	if err != nil {
		return err
	}

	for _, entry := range index {
		fmt.Fprintf(w, "%s\n", string(entry["path"]))
	}

	return nil
}

func CatFile(
		r io.ReadSeeker, w io.Writer, chunks []SeekPoint, offset int64,
		algo *CompressAlgo, global map[string][]byte) error {
	chunk := SeekPoint{0, 0}
	for i := len(chunks) - 1; i >= 0; i-- {
		ch := chunks[i]
		if ch.RawOffset < offset {
			chunk = ch
			break
		}
	}

	toSkip := offset - chunk.RawOffset

	_, err := r.Seek(chunk.CompressedOffset, io.SeekStart)
	if err != nil {
		return err
	}

	zr, err := algo.NewReader(r)
	if err != nil {
		return err
	}

	_, err = io.CopyN(io.Discard, zr, toSkip)
	if err != nil {
		return err
	}

	headerBlock := Block{}

	next := map[string][]byte{}

	for {
		err := ReadAll(zr, headerBlock[:])
		if err != nil {
			return err
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

		headerTypeCh := headerBlock[HDR_TYPE.Offset]
		switch ParseType(headerTypeCh) {
		case HDR_TYPE_FILE: fallthrough
		case HDR_TYPE_HARDLINK: fallthrough
		case HDR_TYPE_SYMLINK: fallthrough
		case HDR_TYPE_CHARDEV: fallthrough
		case HDR_TYPE_BLOCKDEV: fallthrough
		case HDR_TYPE_DIR: fallthrough
		case HDR_TYPE_FIFO:
			_, err = io.CopyN(w, zr, size)
			if err != nil {
				return err
			}

			return nil

		case HDR_TYPE_PAX_GLOBAL:
			content, _, err := ReadFileContent(zr, size)
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

		case HDR_TYPE_PAX_NEXT:
			content, _, err := ReadFileContent(zr, size)

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

		case HDR_TYPE_GNU_PATH:
			content, _, err := ReadFileContent(zr, size)
			if err != nil {
				return err
			}

			next["path"] = content

		case HDR_TYPE_GNU_LINK_PATH:
			content, _, err := ReadFileContent(zr, size)
			if err != nil {
				return err
			}

			next["linkpath"] = content

		default:
			return fmt.Errorf("Unexpected file entry type in tar: '%c'", headerTypeCh)
		}
	}
}

func CatFiles(r io.ReadSeeker, w io.Writer, files []string) error {
	indexOffset, chunksOffset, algo, err := FindTail(r)
	if err != nil {
		return err
	}

	index, err := ReadIndex(r, indexOffset, algo)
	if err != nil {
		return err
	}

	chunks, err := ReadChunks(r, chunksOffset, algo)
	if err != nil {
		return err
	}

	for _, file := range files {
		found := false

		for _, entry := range index {
			if !bytes.Equal(entry["path"], []byte(file)) {
				continue
			}

			found = true

			offset, err := strconv.ParseInt(string(entry["scar:offset"]), 10, 64)
			if err != nil {
				return err
			}

			global := map[string][]byte{}
			for k, v := range entry {
				global[k] = v
			}

			pos, err := r.Seek(0, io.SeekCurrent)
			if err != nil {
				return err
			}

			err = CatFile(r, w, chunks, offset, algo, entry)
			if err != nil {
				return err
			}

			_, err = r.Seek(pos, io.SeekStart)
			if err != nil {
				return err
			}

			break
		}

		if !found {
			return fmt.Errorf("No file entry: %s", file)
		}
	}

	return nil
}

const (
	FLAG_IFILE = iota
	FLAG_OFILE
	FLAG_SIZE
	FLAG_ENUM
	FLAG_BOOL
)

type cliFlag struct {
	longname string;
	shortname rune;
	typ int;
	ptr interface{};
	desc string;
	choices []string;
}

func iFileFlag(long string, short rune, ptr **os.File, desc string) cliFlag {
	return cliFlag{long, short, FLAG_IFILE, ptr, desc, nil}
}

func oFileFlag(long string, short rune, ptr **os.File, desc string) cliFlag {
	return cliFlag{long, short, FLAG_OFILE, ptr, desc, nil}
}

func sizeFlag(long string, short rune, ptr *int64, desc string) cliFlag {
	return cliFlag{long, short, FLAG_SIZE, ptr, desc, nil}
}

func enumFlag(long string, short rune, ptr *string, desc string, choices ...string) cliFlag {
	return cliFlag{long, short, FLAG_ENUM, ptr, desc, choices}
}

func boolFlag(long string, short rune, ptr *bool, desc string) cliFlag {
	return cliFlag{long, short, FLAG_BOOL, ptr, desc, nil}
}

func parseFlag(flag *cliFlag, val string) error {
	if flag.typ == FLAG_IFILE {
		f, err := os.Open(val)
		if err != nil {
			return err
		}
		*flag.ptr.(**os.File) = f
	} else if flag.typ == FLAG_OFILE {
		f, err := os.Create(val)
		if err != nil {
			return err
		}
		*flag.ptr.(**os.File) = f
	} else if flag.typ == FLAG_SIZE {
		n, err := strconv.ParseInt(val, 10, 64)
		if err != nil {
			return fmt.Errorf("%s: %w\n", val, err)
		}
		*flag.ptr.(*int64) = n
	} else if flag.typ == FLAG_ENUM {
		match := false
		for _, v := range flag.choices {
			if v == val {
				match = true
				break
			}
		}

		if !match {
			return fmt.Errorf("Expected one of: %v, got '%s'", flag.choices, val)
		}

		*flag.ptr.(*string) = val
	} else if flag.typ == FLAG_BOOL {
		if val == "false" {
			*flag.ptr.(*bool) = false
		} else if val == "true" {
			*flag.ptr.(*bool) = true
		} else {
			return fmt.Errorf("Expected 'true' or 'false', got '%s'", val)
		}
	} else {
		return errors.New("Invalid flag")
	}

	return nil
}

func parseFlags(argv []string, flags []cliFlag) ([]string, error) {
	newArgv := []string{}

	var i int
	for i = 1; i < len(argv); i++ {
		arg := argv[i]
		if arg == "--" {
			i += 1
			break
		}

		var match *cliFlag = nil
		key := ""
		var val *string = nil
		if arg[:1] == "-" {
			if idx := strings.Index(arg, "="); idx >= 0 {
				key = arg[:idx]
				v := arg[idx + 1:]
				val = &v
			} else {
				key = arg
			}

			for _, f := range flags {
				if key == "-" + string(f.shortname) {
					match = &f
					break
				} else if key[:2] == "-" + string(f.shortname) {
					match = &f
					v := key[2:]
					val = &v
					key = key[:2]
					break
				} else if key == "--" + arg {
					match = &f
					break
				} else if f.typ == FLAG_BOOL && val == nil && arg == "--no-" + f.longname {
					match = &f
					v := "false"
					val = &v
					break
				}
			}
		}

		if arg[:1] == "-" {
			if match == nil {
				return nil, errors.New("Unknown flag: " + arg)
			}

			if val == nil && match.typ == FLAG_BOOL {
				v := "true"
				val = &v
			}

			if val == nil && i >= len(argv) - 1 {
				return nil, errors.New("Flag " + key + " requires an argument")
			} else if val == nil {
				i += 1
				val = &argv[i]
			}

			err := parseFlag(match, *val)
			if err != nil {
				return nil, fmt.Errorf("Invalid value for %s: %w", key, err)
			}
		} else {
			newArgv = append(newArgv, arg)
		}
	}

	for j := i; j < len(argv); j++ {
		newArgv = append(newArgv, argv[j])
	}

	return newArgv, nil
}

func usage(w io.Writer, flags []cliFlag) {
	fmt.Fprintf(w, "Usage:\n")
	fmt.Fprintf(w,
		"  scar [options] create\n" +
		"        Create a scar archive from the input file.\n"+
		"        Uses the compression algorithm given by the 'compression' option.\n")
	fmt.Fprintf(w,
		"  scar [options] cat patterns...\n" +
		"        Read files matching the patterns\n")
	fmt.Fprintf(w,
		"  scar [options] subset patterns...\n" +
		"        Output a tarball with files matching the given patterns.\n")
	fmt.Fprintf(w,
		"  scar [options] list\n" +
		"        List arcive contents\n")

	fmt.Fprintf(w, "\nOptions:\n")
	for _, flag := range flags {
		fmt.Fprintf(w,
			"  --%s, -%c:\n" +
			"        %s\n",
			flag.longname, flag.shortname, flag.desc)
	}

	fmt.Fprintf(w, "\nExamples:\n")
	fmt.Fprintf(w,
		"  tar c dir | scar create -o archive.tgz\n" +
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

func main() {
	var err error

	doHelp := false
	inFile := os.Stdin
	outFile := os.Stdout
	blockSize := int64(256 * 1024)
	compressionFlag := "gzip"

	flags := []cliFlag {
		boolFlag("help", 'h', &doHelp, "Show this help text."),
		iFileFlag("in", 'i', &inFile, "Input file ('-' for stdout)."),
		oFileFlag("out", 'o', &outFile, "Output file ('-' for stdin)."),
		sizeFlag("blocksize", 'b', &blockSize, "Approximate distance between seekpoints."),
		enumFlag("compression", 'c', &compressionFlag, "Which compression algorithm to use.",
			"gzip"),
	}

	argv, err := parseFlags(os.Args, flags)

	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n\n", err.Error())
		usage(os.Stderr, flags)
		os.Exit(1)
	}

	if doHelp {
		usage(os.Stdout, flags)
		os.Exit(0)
	}

	algo := CompressAlgos[compressionFlag]

	if len(argv) < 1 {
		usage(os.Stderr, flags)
		os.Exit(1)
	}

	subcommand := argv[0]
	argv = argv[1:]
	switch subcommand {
	case "create":
		err = CreateIndexedCompressedTar(inFile, outFile, blockSize, &algo)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}

	case "cat":
		err = CatFiles(inFile, outFile, argv)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}

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
		fmt.Fprintf(os.Stderr, "Unknown subcommand: %s\n\n", subcommand)
		usage(os.Stderr, flags)
		os.Exit(1)
	}
}
