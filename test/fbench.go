// The fakio server benchmark tool
// from github.com/shadowsocks/shadowsocks-go

package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"strconv"
)

const (
	TestName   = "serho"
	TestPass   = "123456"
	TestServer = "localhost:8888"
)

//crypto
func stringToKey(str string) (key []byte) {
	hash := sha256.New()
	hash.Write([]byte(str))
	k := hash.Sum(nil)
	return k
}

type Cipher struct {
	handEnc cipher.Stream
	handDec cipher.Stream
	enc     cipher.Stream
	dec     cipher.Stream
}

func (c *Cipher) InitCrypt(bytes []byte) error {

	block, err := aes.NewCipher(bytes[32:])
	if err != nil {
		return err
	}

	c.enc = cipher.NewCFBEncrypter(block, bytes[0:16])
	c.dec = cipher.NewCFBDecrypter(block, bytes[16:32])

	return nil
}

func (c *Cipher) Encrypt(dst, src []byte) {
	c.enc.XORKeyStream(dst, src)
}

func (c *Cipher) Decrypt(dst, src []byte) {
	c.dec.XORKeyStream(dst, src)
}

// Connection Context
type FakioConn struct {
	net.Conn
	*Cipher
}

func NewFakioConn(cn net.Conn, cipher *Cipher) *FakioConn {
	return &FakioConn{cn, cipher}
}

func BuildClientReq(addr string) (buf []byte, err error) {

	buf = make([]byte, 1024)

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
	nameLen := len(TestName)

	index := 16 + 1 + nameLen
	buf[16] = byte(nameLen)
	copy(buf[17:index], TestName)
	buf[index] = 0x5
	buf[index+1] = 0x3
	buf[index+2] = byte(hostLen)
	copy(buf[index+3:], host)
	binary.BigEndian.PutUint16(buf[index+3+hostLen:], uint16(port))

	return buf, nil
}

func FakioDial(req []byte, server string, cipher *Cipher) (c *FakioConn, err error) {
	conn, err := net.Dial("tcp", server)
	if err != nil {
		return
	}
	c = NewFakioConn(conn, cipher)
	if _, err = c.Write(req); err != nil {
		c.Close()
		return nil, err
	}
	return
}

func Dial(addr, server string, cipher *Cipher) (c *FakioConn, err error) {
	ra, err := BuildClientReq(addr)
	if err != nil {
		return
	}
	return FakioDial(ra, server, cipher)
}

func (c *FakioConn) Read(b []byte) (n int, err error) {

	if c.handDec == nil {
		iv := make([]byte, 16)
		if _, err = io.ReadFull(c.Conn, iv); err != nil {
			return
		}
		key := stringToKey(TestPass)
		block, err := aes.NewCipher(key)
		if err != nil {
			return 0, err
		}
		c.handEnc = cipher.NewCFBEncrypter(block, iv)
		n, err = c.Conn.Read(b)
		c.handEnc.XORKeyStream(b, b)
		return n, err
	}
	return
}

func (c *FakioConn) Write(b []byte) (n int, err error) {
	if c.handEnc == nil {
		key := stringToKey(TestPass)
		block, err := aes.NewCipher(key)
		if err != nil {
			return 0, err
		}
		c.handEnc = cipher.NewCFBEncrypter(block, b[0:16])

		// 22 = iv + username
		c.handEnc.XORKeyStream(b[22:], b[22:])
		n, err := c.Conn.Write(b)
		return n, err
	}
	return
}

func main() {
	req, _ := http.NewRequest("GET", "http://example.com/test", nil)
	req.Header.Set("User-Agent", "fbench")

	var b bytes.Buffer
	req.Write(&b)

	cipher := new(Cipher)

	conn, err := Dial("localhost:8000", TestServer, cipher)
	if err != nil {
		fmt.Println(err)
	}

	bytes := make([]byte, 48)
	_, err = conn.Read(bytes)
	if err != nil {
		fmt.Println(err)
	}

	err = conn.InitCrypt(bytes)
	if err != nil {
		fmt.Println(err)
	}

	conn.Write(b.Bytes())

	reqs := make([]byte, 1024)
	conn.Read(reqs)
	fmt.Println(reqs)
}
