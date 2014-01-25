// The Fakio Client write by go
// thank github.com/shadowsocks/shadowsocks-go

package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"flag"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

const (
	TestName   = "serho"
	TestPass   = "123456"
	TestServer = "localhost:8888"
)

// The Cipher
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

// The Fakio connection
type FakioConn struct {
	net.Conn
	*Cipher
}

func FakioDial(server string, req []byte) (c *FakioConn, err error) {
	conn, err := net.Dial("tcp", server)
	if err != nil {
		return nil, err
	}

	//handshake
	key := stringToKey(TestPass)

	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	enc := cipher.NewCFBEncrypter(block, req[0:16])
	// 22 = iv + username
	enc.XORKeyStream(req[22:], req[22:])
	if _, err := conn.Write(req); err != nil {
		return nil, ErrHandToServer
	}

	hand := make([]byte, 64)
	if _, err := conn.Read(hand); err != nil {
		return nil, ErrHandFromServer
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
