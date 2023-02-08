package main

import (
	"bufio"
	"bytes"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

const (
	hdrTypeFile = iota
	hdrTypeHardlink
	hdrTypeSymlink
	hdrTypeChardev
	hdrTypeBlockdev
	hdrTypeDir
	hdrTypeFifo
	hdrTypePaxNext
	hdrTypePaxGlobal
	hdrTypeGnuPath
	hdrTypeGnuLinkPath
	hdrTypeUnknown
)

var (
	// Old TAR header
	hdrPath     = hdrField{0, 100}
	hdrMode     = hdrFieldAfter(hdrPath, 8)
	hdrOwnerUid = hdrFieldAfter(hdrMode, 8)
	hdrOwnerGid = hdrFieldAfter(hdrOwnerUid, 8)
	hdrSize     = hdrFieldAfter(hdrOwnerGid, 12)
	hdrMtime    = hdrFieldAfter(hdrSize, 12)
	hdrChksum   = hdrFieldAfter(hdrMtime, 8)
	hdrType     = hdrFieldAfter(hdrChksum, 1)
	hdrLinkPath = hdrFieldAfter(hdrType, 100)

	// UStar extension
	hdrMagic      = hdrFieldAfter(hdrLinkPath, 6)
	hdrVersion    = hdrFieldAfter(hdrMagic, 2)
	hdrOwnerUname = hdrFieldAfter(hdrVersion, 32)
	hdrOwnerGname = hdrFieldAfter(hdrOwnerUname, 32)
	hdrDevmajor   = hdrFieldAfter(hdrOwnerGname, 8)
	hdrDevminor   = hdrFieldAfter(hdrDevmajor, 8)
	hdrPrefix     = hdrFieldAfter(hdrDevminor, 155)
)

type compressedWriter interface {
	Close() error
	Reset(io.Writer)
	Write([]byte) (int, error)
}

type cmdWriter struct {
	name string
	args []string
	cmd *exec.Cmd
	stdin io.WriteCloser
	stdout io.ReadCloser
	errCh chan error
}

func newCmdWriter(w io.Writer, name string, args ...string) *cmdWriter {
	cw := cmdWriter{name, args, nil, nil, nil, nil}
	err := cw.start(w)
	if err != nil {
		cw.errCh <- err
	}

	return &cw
}

func (cw *cmdWriter) start(w io.Writer) error {
	cw.cmd = exec.Command(cw.name, cw.args...)

	var err error

	cw.stdin, err = cw.cmd.StdinPipe()
	if err != nil {
		return err
	}

	cw.stdout, err = cw.cmd.StdoutPipe()
	if err != nil {
		return err
	}

	cw.errCh = make(chan error, 1)

	go func() {
		buf := make([]byte, 16 * 1024)
		for {
			n, err := cw.stdout.Read(buf[:])
			if err != nil {
				cw.errCh <- err
				return
			}

			err = writeAll(w, buf[:n])
			if err != nil {
				cw.errCh <- err
				return
			}
		}
	}()

	return cw.cmd.Start()
}

func (cw *cmdWriter) Close() error {
	err := cw.stdin.Close()
	if err != nil {
		return err
	}

	err = <-cw.errCh
	if err != io.EOF {
		return err
	}

	err = cw.cmd.Wait()
	if err != nil {
		return err
	}

	cw.cmd = nil
	return nil
}

func (cw *cmdWriter) Reset(w io.Writer) {
	if cw.cmd != nil {
		err := cw.Close()
		if err != nil {
			cw.errCh <- err
		}
	}

	err := cw.start(w)
	if err != nil {
		cw.errCh <- err
	}
}

func (cw *cmdWriter) Write(buf []byte) (int, error) {
	select {
	case err := <-cw.errCh:
		return 0, err
	default:
	}

	return cw.stdin.Write(buf)
}

type cmdReader struct {
	name string
	args []string
	cmd *exec.Cmd
	stdin io.WriteCloser
	stdout io.ReadCloser
	errCh chan error
}

func newCmdReader(r io.Reader, name string, args ...string) (*cmdReader, error) {
	cr := cmdReader{name, args, nil, nil, nil, nil}
	err := cr.start(r)
	if err != nil {
		return nil, err
	}

	return &cr, nil
}

func (cr *cmdReader) start(r io.Reader) error {
	cr.cmd = exec.Command(cr.name, cr.args...)

	var err error

	cr.stdin, err = cr.cmd.StdinPipe()
	if err != nil {
		return err
	}

	cr.stdout, err = cr.cmd.StdoutPipe()
	if err != nil {
		return err
	}

	cr.errCh = make(chan error, 0)

	go func() {
		buf := make([]byte, 16 * 1024)
		for {
			n, err := r.Read(buf[:])

			if err != nil {
				cr.stdin.Close()
				cr.errCh <- err
				return
			}

			err = writeAll(cr.stdin, buf[:n])
			if err != nil {
				cr.stdin.Close()
				cr.errCh <- err
				return
			}
		}
	}()

	return cr.cmd.Start()
}

func (cr *cmdReader) Close() error {
	err := cr.stdin.Close()
	if err != nil {
		return err
	}

	err = cr.cmd.Wait()
	if err != nil {
		return err
	}

	err = <-cr.errCh
	if err != io.EOF {
		return err
	}

	return nil
}

func (cr *cmdReader) Read(buf []byte) (int, error) {
	numRead := 0
	toRead := len(buf)
	for toRead > 0 {
		n, err := cr.stdout.Read(buf[numRead:])
		numRead += n
		toRead -= n
		if err != nil {
			return numRead, err
		}
	}

	return numRead, nil
}

type compressAlgo struct {
	Magic     []byte
	NewReader func(io.Reader) (io.ReadCloser, error)
	NewWriter func(io.Writer) compressedWriter
}

var compressAlgos = map[string]compressAlgo{
	"gzip": {
		Magic: []byte{0x1f, 0x8b},
		NewReader: func(r io.Reader) (io.ReadCloser, error) {
			return gzip.NewReader(r)
		},
		NewWriter: func(w io.Writer) compressedWriter {
			return gzip.NewWriter(w)
		},
	},
	"bzip2": {
		Magic: []byte{0x42, 0x5a, 0x68},
		NewReader: func(r io.Reader) (io.ReadCloser, error) {
			return newCmdReader(r, "bzip2", "-d")
		},
		NewWriter: func(w io.Writer) compressedWriter {
			return newCmdWriter(w, "bzip2")
		},
	},
	"xz": {
		Magic: []byte{0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00},
		NewReader: func(r io.Reader) (io.ReadCloser, error) {
			return newCmdReader(r, "xz", "-d")
		},
		NewWriter: func(w io.Writer) compressedWriter {
			return newCmdWriter(w, "xz")
		},
	},
	"zstd": {
		Magic: []byte{0x28, 0xb5, 0x2f, 0xfd},
		NewReader: func(r io.Reader) (io.ReadCloser, error) {
			return newCmdReader(r, "zstd", "-d")
		},
		NewWriter: func(w io.Writer) compressedWriter {
			return newCmdWriter(w, "zstd")
		},
	},
}

type hdrField struct {
	Offset uint
	Length uint
}

func hdrFieldAfter(other hdrField, size uint) hdrField {
	return hdrField{other.Offset + other.Length, size}
}

type block = [512]byte

func log10Ceil(num int) int {
	log := 0
	for num > 0 {
		log += 1
		num /= 10
	}
	return log
}

func readAll(r io.Reader, buf []byte) error {
	n, err := r.Read(buf)
	if err != nil {
		return err
	} else if n != len(buf) {
		return errors.New("Short read")
	}

	return nil
}

func writeAll(w io.Writer, buf []byte) error {
	n, err := w.Write(buf)
	if err != nil {
		return err
	} else if n != len(buf) {
		return errors.New("Short write")
	}

	return nil
}

func roundBlock(size int64) int64 {
	// I hope Go optimizes this?
	for size%512 != 0 {
		size += 1
	}

	return size
}

func bufIsZero(buf []byte) bool {
	for _, ch := range buf {
		if ch != 0 {
			return false
		}
	}

	return true
}

func parseType(ch byte) int {
	switch ch {
	case 0:
		return hdrTypeFile
	case '0':
		return hdrTypeFile
	case '1':
		return hdrTypeHardlink
	case '2':
		return hdrTypeSymlink
	case '3':
		return hdrTypeChardev
	case '4':
		return hdrTypeBlockdev
	case '5':
		return hdrTypeDir
	case '6':
		return hdrTypeFifo
	case '7':
		return hdrTypeFile // really contiguous file but we treat it like a normal file
	case 'x':
		return hdrTypePaxNext
	case 'g':
		return hdrTypePaxGlobal
	case 'L':
		return hdrTypeGnuPath
	case 'K':
		return hdrTypeGnuLinkPath
	default:
		return hdrTypeUnknown
	}
}

func parseStringField(block *block, field hdrField) []byte {
	var strlength uint = 0
	for {
		if block[field.Offset+strlength] == 0 {
			break
		}

		strlength += 1
	}

	return block[field.Offset : field.Offset+strlength]
}

func parsePathField(block *block, field hdrField) []byte {
	path := parseStringField(block, field)
	if block[hdrPrefix.Offset] == 0 {
		return path
	} else {
		return append(append(parseStringField(block, hdrPrefix), byte('/')), path...)
	}
}

func parseOctalField(block *block, field hdrField) int64 {
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

func parseOctal255Field(block *block, field hdrField) int64 {
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

	return parseOctalField(block, field)
}

func readFileContent(r io.Reader, size int64) ([]byte, []byte, error) {
	buf := make([]byte, roundBlock(size))
	err := readAll(r, buf)
	if err != nil {
		return nil, nil, err
	}

	return buf[:size], buf, nil
}

func dumpFileContent(r io.Reader, w io.Writer, size int64) error {
	block := block{}
	for size > 0 {
		err := readAll(r, block[:])
		if err != nil {
			return err
		}

		err = writeAll(w, block[:])
		if err != nil {
			return err
		}

		size -= 512
	}

	return nil
}

func writePaxSyntax(w io.Writer, content map[string][]byte) error {
	for k, v := range content {
		size := 0
		size += 1 + len(k) + 1 + len(v) + 1 // ' ' key '=' val '\n'

		digitCount := log10Ceil(size)
		digits := fmt.Sprintf("%d", size+digitCount)
		if len(digits) != digitCount {
			digits = fmt.Sprintf("%d", size+digitCount+1)
		}

		_, err := fmt.Fprintf(w, "%s %s=", digits, k)
		if err != nil {
			return err
		}

		err = writeAll(w, v)
		if err != nil {
			return err
		}

		err = writeAll(w, []byte("\n"))
		if err != nil {
			return err
		}
	}

	return nil
}

func parsePaxSyntax(content []byte) (map[string][]byte, error) {
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
		copy(payload, content[offset:offset+payloadLength])
		offset += payloadLength

		if content[offset] != '\n' {
			return pax, errors.New("Missing newline at the end of field")
		}
		offset += 1

		pax[keyword] = payload
	}

	return pax, nil
}

type tarWriter interface {
	io.Writer
	Flush() error
	Written() int64
}

type basicTarWriter struct {
	w            io.Writer
	WrittenCount int64
}

func (tw *basicTarWriter) Write(p []byte) (int, error) {
	n, err := tw.w.Write(p)
	if err != nil {
		return n, err
	}

	tw.WrittenCount += int64(n)
	return n, err
}

func (tw *basicTarWriter) Written() int64 {
	return tw.WrittenCount
}

func (tw *basicTarWriter) Flush() error {
	return nil
}

func newBasicTarWriter(w io.Writer) *basicTarWriter {
	return &basicTarWriter{w, 0}
}

type seekPoint struct {
	RawOffset        int64
	CompressedOffset int64
}

type compressedTarWriter struct {
	W          *basicTarWriter
	Z          compressedWriter
	rawWritten int64
	Flushes    []seekPoint
}

func (tw *compressedTarWriter) Write(p []byte) (int, error) {
	tw.rawWritten += int64(len(p))
	return tw.Z.Write(p)
}

func (tw *compressedTarWriter) Written() int64 {
	return tw.W.Written()
}

func (tw *compressedTarWriter) Flush() error {
	err := tw.Z.Close()
	if err != nil {
		return err
	}

	tw.Flushes = append(tw.Flushes, seekPoint{tw.rawWritten, tw.W.Written()})

	tw.Z.Reset(tw.W)
	return nil
}

func (tw *compressedTarWriter) Close() error {
	return tw.Z.Close()
}

func createIndexedTar(r io.Reader, w tarWriter, iw io.Writer, flushThreshold int64) error {
	headerBlock := block{}

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

		err := readAll(r, headerBlock[:])
		if err != nil {
			return err
		}

		// Two all-zero blocks in a row is an end marker
		if bufIsZero(headerBlock[:]) {
			err = writeAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = readAll(r, headerBlock[:])
			if err != nil {
				return err
			}

			err = writeAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			if !bufIsZero(headerBlock[:]) {
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
			size = parseOctal255Field(&headerBlock, hdrSize)
		}

		writeIndexEntry := true
		isUnknown := false

		switch parseType(headerBlock[hdrType.Offset]) {
		case hdrTypeFile:
			fallthrough
		case hdrTypeHardlink:
			fallthrough
		case hdrTypeSymlink:
			fallthrough
		case hdrTypeChardev:
			fallthrough
		case hdrTypeBlockdev:
			fallthrough
		case hdrTypeDir:
			fallthrough
		case hdrTypeFifo:
			err := writeAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = dumpFileContent(r, w, size)
			if err != nil {
				return err
			}

			next = map[string][]byte{}

		case hdrTypePaxGlobal:
			writeIndexEntry = false

			content, buf, err := readFileContent(r, size)
			if err != nil {
				return err
			}

			pax, err := parsePaxSyntax(content)
			if err != nil {
				return err
			}

			for k, v := range pax {
				global[k] = v
			}

			err = writeAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = writeAll(w, buf)
			if err != nil {
				return err
			}

		case hdrTypePaxNext:
			content, buf, err := readFileContent(r, size)
			writeIndexEntry = false

			if err != nil {
				return err
			}

			pax, err := parsePaxSyntax(content)
			if err != nil {
				return err
			}

			for k, v := range pax {
				next[k] = v
			}

			err = writeAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = writeAll(w, buf)
			if err != nil {
				return err
			}

		case hdrTypeGnuPath:
			writeIndexEntry = false

			content, buf, err := readFileContent(r, size)
			if err != nil {
				return err
			}

			next["path"] = content

			err = writeAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = writeAll(w, buf)
			if err != nil {
				return err
			}

		case hdrTypeGnuLinkPath:
			writeIndexEntry = false

			content, buf, err := readFileContent(r, size)
			if err != nil {
				return err
			}

			next["linkpath"] = content

			err = writeAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = writeAll(w, buf)
			if err != nil {
				return err
			}

		default:
			writeIndexEntry = false
			isUnknown = true

			err := writeAll(w, headerBlock[:])
			if err != nil {
				return err
			}

			err = dumpFileContent(r, w, size)
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
				path = parsePathField(&headerBlock, hdrPath)
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
			writePaxSyntax(&indexEntryBuffer, pax)

			indexEntrySize := 1 + indexEntryBuffer.Len() // ' ' entry
			digitCount := log10Ceil(indexEntrySize)
			digits := fmt.Sprintf("%d", indexEntrySize+digitCount)
			if len(digits) != digitCount {
				digits = fmt.Sprintf("%d", indexEntrySize+digitCount+1)
			}

			_, err = fmt.Fprintf(iw, "%s ", digits)
			if err != nil {
				return err
			}

			err = writeAll(iw, indexEntryBuffer.Bytes())
			if err != nil {
				return err
			}
		}

		offset += 512 + roundBlock(size)
		if writeIndexEntry || isUnknown {
			fileMetaStart = offset
		}
	}

	return nil
}

func createIndexedCompressedTar(r io.Reader, w io.Writer, thresh int64, algo *compressAlgo) error {
	indexBuffer := bytes.Buffer{}
	_, err := fmt.Fprintf(&indexBuffer, "SCAR-INDEX\n")
	if err != nil {
		return err
	}

	btw := newBasicTarWriter(w)
	ctw := compressedTarWriter{btw, algo.NewWriter(btw), 0, []seekPoint{}}
	defer ctw.Close()

	err = createIndexedTar(r, &ctw, &indexBuffer, thresh)
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

func findTail(r io.ReadSeeker) (int64, int64, *compressAlgo, error) {
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
	tailMagic := []byte("SCAR-TAIL\n")

	for {
		lastIndex := -1
		var lastAlgo compressAlgo
		for _, algo := range compressAlgos {
			idx := bytes.LastIndex(zTailBuf[:zTailBufLength], algo.Magic)
			if idx > lastIndex {
				lastIndex = idx
				lastAlgo = algo
			}
		}

		if lastIndex < 0 {
			return 0, 0, nil, errors.New("Couldn't find scar footer at the end of archive.")
		}

		zTailReader := bytes.NewReader(zTailBuf[lastIndex:])
		tailReader, err := lastAlgo.NewReader(zTailReader)
		if err != nil {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			fmt.Fprintf(os.Stderr, "Couldn't create reader: %v\n", err)
			continue
		}

		n, err := tailReader.Read(tailBuf[:])
		if err != nil && err != io.EOF {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			fmt.Fprintf(os.Stderr, "Couldn't read (%d): %v\n", n, err)
			continue
		}

		if n < len(tailMagic) || !bytes.Equal(tailBuf[:len(tailMagic)], tailMagic) {
			zTailBufLength = lastIndex + len(lastAlgo.Magic) - 1
			fmt.Fprintf(os.Stderr, "Bytes not equal (%v)\n", tailBuf[:len(tailMagic)])
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

		return indexOffset, chunksOffset, &lastAlgo, nil
	}
}

func readIndexEntry(r *bufio.Reader, tmpbuf *bytes.Buffer) (map[string][]byte, error) {
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

	return parsePaxSyntax(tmpbuf.Bytes())
}

func readIndex(r io.ReadSeeker, indexOffset int64, algo *compressAlgo) ([]map[string][]byte, error) {
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

		pax, err := readIndexEntry(indexBufReader, tmpbuf)
		if err == io.EOF {
			break
		} else if err != nil {
			return nil, err
		}

		index = append(index, pax)
	}

	return index, nil
}

func readChunks(r io.ReadSeeker, chunksOffset int64, algo *compressAlgo) ([]seekPoint, error) {
	_, err := r.Seek(chunksOffset, io.SeekStart)
	if err != nil {
		return nil, err
	}

	chunksReader, err := algo.NewReader(r)
	if err != nil {
		return nil, err
	}
	defer chunksReader.Close()

	chunks := []seekPoint{}

	chunksScanner := bufio.NewScanner(chunksReader)

	chunksMagic := []byte("SCAR-CHUNKS")
	if !chunksScanner.Scan() || !bytes.Equal(chunksScanner.Bytes(), chunksMagic) {
		return nil, errors.New("Missing SCAR-CHUNKS marker")
	}

	tailMagic := []byte("SCAR-TAIL")
	for chunksScanner.Scan() {
		line := chunksScanner.Bytes()
		if bytes.Equal(line, tailMagic) {
			return chunks, nil
		}

		var chunk seekPoint
		fmt.Sscanf(string(line), "%d %d", &chunk.CompressedOffset, &chunk.RawOffset)
		chunks = append(chunks, chunk)
	}

	return chunks, nil
}

func listArchive(r io.ReadSeeker, w io.Writer) error {
	indexOffset, _, algo, err := findTail(r)
	if err != nil {
		return err
	}

	index, err := readIndex(r, indexOffset, algo)
	if err != nil {
		return err
	}

	for _, entry := range index {
		fmt.Fprintf(w, "%s\n", string(entry["path"]))
	}

	return nil
}

func catFile(
	r io.ReadSeeker, w io.Writer, chunks []seekPoint, offset int64,
	algo *compressAlgo, global map[string][]byte) error {
	chunk := seekPoint{0, 0}
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

	headerBlock := block{}

	next := map[string][]byte{}

	for {
		err := readAll(zr, headerBlock[:])
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
			size = parseOctal255Field(&headerBlock, hdrSize)
		}

		headerTypeCh := headerBlock[hdrType.Offset]
		switch parseType(headerTypeCh) {
		case hdrTypeFile:
			fallthrough
		case hdrTypeHardlink:
			fallthrough
		case hdrTypeSymlink:
			fallthrough
		case hdrTypeChardev:
			fallthrough
		case hdrTypeBlockdev:
			fallthrough
		case hdrTypeDir:
			fallthrough
		case hdrTypeFifo:
			_, err = io.CopyN(w, zr, size)
			if err != nil {
				return err
			}

			return nil

		case hdrTypePaxGlobal:
			content, _, err := readFileContent(zr, size)
			if err != nil {
				return err
			}

			pax, err := parsePaxSyntax(content)
			if err != nil {
				return err
			}

			for k, v := range pax {
				global[k] = v
			}

		case hdrTypePaxNext:
			content, _, err := readFileContent(zr, size)

			if err != nil {
				return err
			}

			pax, err := parsePaxSyntax(content)
			if err != nil {
				return err
			}

			for k, v := range pax {
				next[k] = v
			}

		case hdrTypeGnuPath:
			content, _, err := readFileContent(zr, size)
			if err != nil {
				return err
			}

			next["path"] = content

		case hdrTypeGnuLinkPath:
			content, _, err := readFileContent(zr, size)
			if err != nil {
				return err
			}

			next["linkpath"] = content

		default:
			return fmt.Errorf("Unexpected file entry type in tar: '%c'", headerTypeCh)
		}
	}
}

func catFiles(r io.ReadSeeker, w io.Writer, files []string) error {
	indexOffset, chunksOffset, algo, err := findTail(r)
	if err != nil {
		return err
	}

	index, err := readIndex(r, indexOffset, algo)
	if err != nil {
		return err
	}

	chunks, err := readChunks(r, chunksOffset, algo)
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

			err = catFile(r, w, chunks, offset, algo, entry)
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
	flagTypeIFile = iota
	flagTypeOFile
	flagTypeSize
	flagTypeEnum
	flagTypeBool
)

type cliFlag struct {
	longname  string
	shortname rune
	typ       int
	ptr       interface{}
	desc      string
	choices   []string
}

func iFileFlag(long string, short rune, ptr **os.File, desc string) cliFlag {
	return cliFlag{long, short, flagTypeIFile, ptr, desc, nil}
}

func oFileFlag(long string, short rune, ptr **os.File, desc string) cliFlag {
	return cliFlag{long, short, flagTypeOFile, ptr, desc, nil}
}

func sizeFlag(long string, short rune, ptr *int64, desc string) cliFlag {
	return cliFlag{long, short, flagTypeSize, ptr, desc, nil}
}

func enumFlag(long string, short rune, ptr *string, desc string, choices ...string) cliFlag {
	return cliFlag{long, short, flagTypeEnum, ptr, desc, choices}
}

func boolFlag(long string, short rune, ptr *bool, desc string) cliFlag {
	return cliFlag{long, short, flagTypeBool, ptr, desc, nil}
}

func parseFlag(flag *cliFlag, val string) error {
	if flag.typ == flagTypeIFile {
		f, err := os.Open(val)
		if err != nil {
			return err
		}
		*flag.ptr.(**os.File) = f
	} else if flag.typ == flagTypeOFile {
		f, err := os.Create(val)
		if err != nil {
			return err
		}
		*flag.ptr.(**os.File) = f
	} else if flag.typ == flagTypeSize {
		n, err := strconv.ParseInt(val, 10, 64)
		if err != nil {
			return fmt.Errorf("%s: %w\n", val, err)
		}
		*flag.ptr.(*int64) = n
	} else if flag.typ == flagTypeEnum {
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
	} else if flag.typ == flagTypeBool {
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
				v := arg[idx+1:]
				val = &v
			} else {
				key = arg
			}

			for _, f := range flags {
				if key == "-"+string(f.shortname) {
					match = &f
					break
				} else if key[:2] == "-"+string(f.shortname) {
					match = &f
					v := key[2:]
					val = &v
					key = key[:2]
					break
				} else if key == "--"+arg {
					match = &f
					break
				} else if f.typ == flagTypeBool && val == nil && arg == "--no-"+f.longname {
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

			if val == nil && match.typ == flagTypeBool {
				v := "true"
				val = &v
			}

			if val == nil && i >= len(argv)-1 {
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
		"  scar [options] create\n"+
			"        Create a scar archive from the input file.\n"+
			"        Uses the compression algorithm given by the 'compression' option.\n")
	fmt.Fprintf(w,
		"  scar [options] cat patterns...\n"+
			"        Read files matching the patterns\n")
	fmt.Fprintf(w,
		"  scar [options] subset patterns...\n"+
			"        Output a tarball with files matching the given patterns.\n")
	fmt.Fprintf(w,
		"  scar [options] list\n"+
			"        List arcive contents\n")

	fmt.Fprintf(w, "\nOptions:\n")
	for _, flag := range flags {
		fmt.Fprintf(w,
			"  --%s, -%c:\n"+
				"        %s\n",
			flag.longname, flag.shortname, flag.desc)
	}

	fmt.Fprintf(w, "\nExamples:\n")
	fmt.Fprintf(w,
		"  tar c dir | scar create -o archive.tgz\n"+
			"        Create a scar file\n")
	fmt.Fprintf(w,
		"  scar -i archive.tgz list\n"+
			"        Read the list of files in the archive\n")
	fmt.Fprintf(w,
		"  scar -i archive.tgz cat ./dir/myfile.txt\n"+
			"        Read a file from an archive\n")
	fmt.Fprintf(w,
		"  scar -i archive.tgz subset './dir/a/*' | tar x\n"+
			"        Extract everything in './dir/a'\n")
}

func main() {
	var err error

	doHelp := false
	inFile := os.Stdin
	outFile := os.Stdout
	blockSize := int64(4 * 1024 * 1024)
	compressionFlag := "auto"

	flags := []cliFlag{
		boolFlag("help", 'h', &doHelp, "Show this help text."),
		iFileFlag("in", 'i', &inFile, "Input file ('-' for stdout)."),
		oFileFlag("out", 'o', &outFile, "Output file ('-' for stdin)."),
		sizeFlag("blocksize", 'b', &blockSize, "Approximate distance between seekpoints."),
		enumFlag("compression", 'c', &compressionFlag, "Which compression algorithm to use.",
			"auto", "gzip", "bzip2", "xz", "zstd"),
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

	if compressionFlag == "auto" {
		path := outFile.Name()
		match := func(suffixes ...string) bool {
			for _, s := range suffixes {
				if strings.HasSuffix(path, s) {
					return true
				}
			}

			return false
		}

		if outFile == os.Stdout {
			compressionFlag = "gzip"
		} else if match(".taz", ".tgz", ".gz") {
			compressionFlag = "gzip"
		} else if match(".tb2", ".tbz", ".tbz2", ".tz2", "bz2") {
			compressionFlag = "bzip2"
		} else if match(".txz", ".xz") {
			compressionFlag = "xz"
		} else if match(".tzst", ".zst") {
			compressionFlag = "zstd"
		} else {
			fmt.Fprintf(os.Stderr, "Couldn't guess compression based on file name: %s\n", path)
			fmt.Fprintf(os.Stderr, "Use '--compression' to set compression algorithm.\n")
			os.Exit(1)
		}
	}

	algo := compressAlgos[compressionFlag]

	if len(argv) < 1 {
		usage(os.Stderr, flags)
		os.Exit(1)
	}

	subcommand := argv[0]
	argv = argv[1:]
	switch subcommand {
	case "create":
		err = createIndexedCompressedTar(inFile, outFile, blockSize, &algo)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}

	case "cat":
		err = catFiles(inFile, outFile, argv)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}

	case "subset":
		fmt.Fprintf(os.Stderr, "Subcommand not implemented: subset\n")
		os.Exit(1)

	case "list":
		err = listArchive(inFile, outFile)
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
