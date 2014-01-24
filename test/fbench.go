// The fakio server benchmark tool
// from github.com/shadowsocks/shadowsocks-go

package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
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
	enc cipher.Stream
	dec cipher.Stream
}

func NewCipher(bytes []byte) (c *Cipher, err error) {
	block, err := aes.NewCipher(bytes[32:])
	if err != nil {
		return c, err
	}

	enc := cipher.NewCFBEncrypter(block, bytes[0:16])
	dec := cipher.NewCFBDecrypter(block, bytes[16:32])

	return &Cipher{enc, dec}, nil
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

func buildClientReq(addr string) (buf []byte, err error) {

	buf = make([]byte, 1024)

	host, portStr, err := net.SplitHostPort(addr)
	if err != nil {
		return nil, errors.New(
			fmt.Sprintf("fakio: address error %s %v", addr, err))
	}
	port, err := strconv.Atoi(portStr)
	if err != nil {
		return nil, errors.New(
			fmt.Sprintf("fakio: invalid port %s", addr))
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

func FakioDial(addr, server string) (c *FakioConn, err error) {
	//fmt.Println("FakioDial")
	conn, err := net.Dial("tcp", server)
	if err != nil {
		return nil, err
	}

	//handshake
	req, err := buildClientReq(addr)
	if err != nil {
		return nil, err
	}

	key := stringToKey(TestPass)
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	enc := cipher.NewCFBEncrypter(block, req[0:16])
	// 22 = iv + username
	enc.XORKeyStream(req[22:], req[22:])
	if _, err := conn.Write(req); err != nil {
		return nil, errors.New("hanshake to server error")
	}

	hand := make([]byte, 64)
	if _, err := conn.Read(hand); err != nil {
		return nil, errors.New("hanshake from server error")
	}

	dec := cipher.NewCFBDecrypter(block, hand[0:16])
	dec.XORKeyStream(hand[16:], hand[16:])

	cipher, err := NewCipher(hand[16:])

	return &FakioConn{conn, cipher}, nil
}

func (c *FakioConn) Read(b []byte) (n int, err error) {

	n, err = c.Conn.Read(b)
	c.Decrypt(b[0:n], b[0:n])

	return n, err
}

func (c *FakioConn) Write(b []byte) (n int, err error) {

	c.Encrypt(b, b)
	n, err = c.Conn.Write(b)

	return n, err
}

func main() {

	tr := &http.Transport{
		Dial: func(_, _ string) (net.Conn, error) {
			return FakioDial("localhost:8000", TestServer)
		},
	}

	client := &http.Client{Transport: tr}

	resp, err := client.Get("http://localhost:8000/")
	if err != nil {
		fmt.Println(err)
		return
	}

	buf, err := ioutil.ReadAll(resp.Body)
	fmt.Println(string(buf))

	if err != io.EOF {
		fmt.Printf("Read response error: %v\n", err)
	} else {
		err = nil
	}

}
