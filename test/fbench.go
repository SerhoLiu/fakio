// The fakio server benchmark tool
// from github.com/shadowsocks/shadowsocks-go

package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/sha256"
	"errors"
	"fmt"
	"net/http"
)

//crypto
func stringToKey(str string) (key []byte) {
	hash := sha256.New()
	hash.Write([]byte(str))
	k := hash.Sum(nil)
	return k
}

type Cipher struct {
	enc cipher.Stream
	dec cipher.Stream
}

func (c *Cipher) New(bytes []byte) (err error) {

	if len(bytes) != 48 {
		err = errors.New("bytes length must 48")
		return
	}
	block, err := aes.NewCipher(bytes[0:16])
	if err != nil {
		return
	}
	c.enc = cipher.NewCFBEncrypter(block, bytes[16:32])
	c.dec = cipher.NewCFBDecrypter(block, bytes[32:48])

	return
}

func (c *Cipher) Encrypt(dst, src []byte) {
	c.enc.XORKeyStream(dst, src)
}

func (c *Cipher) Decrypt(dst, src []byte) {
	c.dec.XORKeyStream(dst, src)
}

// Connection Context
type Context struct {
	net.Conn
	*Cipher
}

func NewConn(cn net.Conn, cipher *Cipher) *Conn {
	return &Conn{cn, cipher}
}

func BuildClientReq(addr, username string) (buf []byte, err error) {
	host, portStr, err := net.SplitHostPort(addr)
	if err != nil {
		return nil, errors.New(
			fmt.Sprintf("shadowsocks: address error %s %v", addr, err))
	}
	port, err := strconv.Atoi(portStr)
	if err != nil {
		return nil, errors.New(
			fmt.Sprintf("shadowsocks: invalid port %s", addr))
	}

	hostLen := len(host)
	l := 1 + 1 + hostLen + 2 // addrType + lenByte + address + port
	buf = make([]byte, l)
	buf[0] = 3             // 3 means the address is domain name
	buf[1] = byte(hostLen) // host address length  followed by host address
	copy(buf[2:], host)
	binary.BigEndian.PutUint16(buf[2+hostLen:2+hostLen+2], uint16(port))
	return
}

// This is intended for use by users implementing a local socks proxy.
// rawaddr shoud contain part of the data in socks request, starting from the
// ATYP field. (Refer to rfc1928 for more information.)
func DialWithRawAddr(rawaddr []byte, server string, cipher *Cipher) (c *Conn, err error) {
	conn, err := net.Dial("tcp", server)
	if err != nil {
		return
	}
	c = NewConn(conn, cipher)
	if _, err = c.Write(rawaddr); err != nil {
		c.Close()
		return nil, err
	}
	return
}

// addr should be in the form of host:port
func Dial(addr, server string, cipher *Cipher) (c *Conn, err error) {
	ra, err := RawAddr(addr)
	if err != nil {
		return
	}
	return DialWithRawAddr(ra, server, cipher)
}

func (c *Conn) Read(b []byte) (n int, err error) {
	if c.dec == nil {
		iv := make([]byte, c.info.ivLen)
		if _, err = io.ReadFull(c.Conn, iv); err != nil {
			return
		}
		if err = c.initDecrypt(iv); err != nil {
			return
		}
	}
	cipherData := make([]byte, len(b))
	n, err = c.Conn.Read(cipherData)
	if n > 0 {
		c.decrypt(b[0:n], cipherData[0:n])
	}
	return
}

func (c *Conn) Write(b []byte) (n int, err error) {
	var cipherData []byte
	dataStart := 0
	if c.enc == nil {
		var iv []byte
		iv, err = c.initEncrypt()
		if err != nil {
			return
		}
		// Put initialization vector in buffer, do a single write to send both
		// iv and data.
		cipherData = make([]byte, len(b)+len(iv))
		copy(cipherData, iv)
		dataStart = len(iv)
	} else {
		cipherData = make([]byte, len(b))
	}
	c.encrypt(cipherData[dataStart:], b)
	n, err = c.Conn.Write(cipherData)
	return
}

func main() {
	req, _ := http.NewRequest("GET", "http://example.com/test", nil)
	req.Header.Set("User-Agent", "fbench")

	var b bytes.Buffer
	req.Write(&b)

	var cipher Cipher

	bytes := make([]byte, 48)

	err := cipher.New(bytes)
	if err != nil {
		fmt.Println(err)
		return
	}

	cipher.Encrypt(b.Bytes(), b.Bytes())
	cipher.Decrypt(b.Bytes(), b.Bytes())
	fmt.Println(b.String())
}
