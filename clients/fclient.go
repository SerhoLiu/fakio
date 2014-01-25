// The Fakio Client write by go
// thank github.com/shadowsocks/shadowsocks-go

package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/sha256"
	//"encoding/binary"
	"errors"
	//"flag"
	"fmt"
	"io"
	"log"
	"net"
	//"net/http"
	//"net/url"
	//"strconv"
	//"strings"
	//"time"
)

type Fclient struct {
	username string
	password string

	server string
	local  string
}

var fclient Fclient
var localReply []byte

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
	key := stringToKey(fclient.password)

	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	enc := cipher.NewCFBEncrypter(block, req[0:16])
	// 22 = iv + username
	enc.XORKeyStream(req[22:], req[22:])
	if _, err := conn.Write(req); err != nil {
		return nil, errors.New("handshake to server error")
	}

	hand := make([]byte, 64)
	if _, err := conn.Read(hand); err != nil {
		return nil, errors.New("handshake to server error")
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

func buildFakioReq(buf []byte) (req []byte, err error) {
	req = make([]byte, 1024)

	iv := make([]byte, 16)
	_, err = rand.Read(iv)
	if err != nil {
		log.Println("rand iv error:", err)
		return
	}
	copy(req, iv)

	nameLen := len(fclient.username)
	index := 16 + 1 + nameLen
	req[16] = byte(nameLen)
	copy(req[17:index], fclient.username)
	req[index] = 0x5
	copy(req[index+1:], buf[3:])

	return req, nil
}

// SOCKS5 Handshake
// http://tools.ietf.org/rfc/rfc1928.txt
func socks5Handshake(conn net.Conn) (req []byte, err error) {

	var n int

	// 264 is max version identifier/method selection message len
	buf := make([]byte, 264)
	n, err = io.ReadAtLeast(conn, buf, 2)
	if err != nil {
		fmt.Println("155")
		return
	}

	// SOCKS VER
	if buf[0] != 0x05 {
		return req, errors.New("only support SOCKS5")
	}
	nmethod := int(buf[1])
	msgLen := nmethod + 2
	if n < msgLen {
		if _, err = io.ReadFull(conn, buf[n:msgLen]); err != nil {
			fmt.Println("167")
			return
		}
	}
	// send confirmation: version 5, no authentication required
	if _, err = conn.Write([]byte{0x05, 0}); err != nil {
		fmt.Println("173")
		return
	}

	// client request
	n, err = io.ReadAtLeast(conn, buf, 5)
	//fmt.Println(n)
	if err != nil {
		fmt.Println("180")
		return
	}
	// check version and cmd
	if buf[0] != 0x05 || buf[1] != 0x01 {
		return req, errors.New("only support socks5 tcp connect")
	}

	//address type: now not support ipv6
	var reqLen = 0
	if buf[3] == 0x01 {
		reqLen = 3 + 1 + net.IPv4len + 2
	} else if buf[3] == 0x03 {
		reqLen = 3 + 1 + 1 + 2 + int(buf[4])
	} else {
		return req, errors.New("address type: only support ipv4 and domain name")
	}

	if n < reqLen {
		if _, err = io.ReadFull(conn, buf[n:reqLen]); err != nil {
			fmt.Println("200")
			return
		}
	}

	req, err = buildFakioReq(buf)
	if err != nil {
		fmt.Println("207")
		return
	}

	//SOCKS5 Replies
	_, err = conn.Write(localReply)
	if err != nil {
		log.Println("send connection confirmation:", err)
		return
	}

	return req, nil
}

// Server
func handleConnection(client net.Conn) {
	log.Println("new connection....")

	defer client.Close()

	req, err := socks5Handshake(client)
	if err != nil {
		log.Println("socks handshake:", err)
		return
	}

	remote, err := FakioDial(fclient.server, req)
	if err != nil {
		log.Println("Failed connect to fakio server: ", err)
		return
	}
	defer remote.Close()

	go func(dst, src net.Conn) {
		buf := make([]byte, 1024)

		for {
			fmt.Println(245)
			n, err := src.Read(buf)
			fmt.Println("247:", err, " ", n)
			if n > 0 {
				if _, err = dst.Write(buf[0:n]); err != nil {
					log.Println("write to remote:", err)
					break
				}
			}
			if err != nil {
				break
			}
		}
	}(client, remote)

	for {

		buf := make([]byte, 1024)

		fmt.Println(256)
		n, err := remote.Read(buf)
		fmt.Println("259:", err, " ", n)
		if n > 0 {
			if _, err = client.Write(buf[0:n]); err != nil {
				log.Println("write to client:", err)
				break
			}
		}
		if err != nil {
			break
		}
	}

	log.Println("closed connection")

}

func run(listenAddr string) {
	ln, err := net.Listen("tcp", listenAddr)
	if err != nil {
		log.Fatal(err)
	}
	log.Printf("starting fakio client(socks5 server) at %v ...\n", listenAddr)
	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Println("accept:", err)
			continue
		}
		go handleConnection(conn)
	}
}

func main() {
	fclient = Fclient{"serho", "123456", "localhost:8888", "localhost:1070"}
	localReply = []byte{0x05, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x42}
	run(fclient.local)
}
