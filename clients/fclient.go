// The Fakio Client write by go
// thank github.com/shadowsocks/shadowsocks-go

package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"os"
	"strconv"
)

type Fclient struct {
	UserName string
	PassWord string

	Server string
	Local  string
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
	key := stringToKey(fclient.PassWord)

	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	enc := cipher.NewCFBEncrypter(block, req[0:16])

	// 22 = iv + username
	index := 16 + int(req[16]) + 1
	enc.XORKeyStream(req[index:], req[index:])
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

	nameLen := len(fclient.UserName)
	index := 16 + 1 + nameLen
	req[16] = byte(nameLen)
	copy(req[17:index], fclient.UserName)
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
			return
		}
	}
	// send confirmation: version 5, no authentication required
	if _, err = conn.Write([]byte{0x05, 0}); err != nil {
		return
	}

	// client request
	n, err = io.ReadAtLeast(conn, buf, 5)
	if err != nil {
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
			return
		}
	}

	req, err = buildFakioReq(buf)
	if err != nil {
		return
	}

	//log request
	var host string
	port := binary.BigEndian.Uint16(buf[reqLen-2 : reqLen])
	if buf[3] == 0x01 {
		host = net.IP(buf[4:8]).String()
	}
	if buf[3] == 0x03 {
		host = string(buf[5 : 5+int(buf[4])])
	}

	addr := net.JoinHostPort(host, strconv.Itoa(int(port)))
	log.Printf("connect %s", addr)

	//SOCKS5 Replies
	_, err = conn.Write(localReply)
	if err != nil {
		return
	}

	return req, nil
}

// Server
func handleConnection(client net.Conn) {

	defer func() {
		client.Close()
	}()

	req, err := socks5Handshake(client)
	if err != nil {
		return
	}

	remote, err := FakioDial(fclient.Server, req)
	if err != nil {
		return
	}
	defer func() {
		remote.Close()
	}()

	go func(dst, src net.Conn) {
		defer func() {
			client.Close()
			remote.Close()
		}()
		buf := make([]byte, 1024)

		for {
			n, err := src.Read(buf)

			if n > 0 {
				if _, err = dst.Write(buf[0:n]); err != nil {
					break
				}
			}
			if err != nil {
				break
			}
		}
	}(remote, client)

	buf := make([]byte, 1024)

	for {
		n, err := remote.Read(buf)

		if n > 0 {
			if _, err = client.Write(buf[0:n]); err != nil {
				break
			}
		}
		if err != nil {
			break
		}
	}
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
			continue
		}
		go handleConnection(conn)
	}
}

func getConfig(path string, fclient *Fclient) (err error) {
	file, err := os.Open(path)
	if err != nil {
		return
	}
	defer file.Close()

	data, err := ioutil.ReadAll(file)
	if err != nil {
		return
	}

	err = json.Unmarshal(data, fclient)
	return
}

func buildReply(addr string) (buf []byte, err error) {

	buf = make([]byte, 264)

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

	buf[0] = 0x05
	buf[1] = 0x00
	buf[2] = 0x00

	ip := net.ParseIP(host).To4()
	if ip != nil {
		buf[3] = 0x01
		buf[4] = ip[3]
		buf[5] = ip[2]
		buf[6] = ip[1]
		buf[7] = ip[0]
		binary.BigEndian.PutUint16(buf[8:], uint16(port))
		return buf[0:10], nil
	}

	hostLen := len(host)
	buf[3] = 0x03
	buf[4] = byte(hostLen)

	copy(buf[5:], host)
	binary.BigEndian.PutUint16(buf[5+hostLen:], uint16(port))

	return buf[0 : 7+hostLen], nil
}

func main() {
	var conf string
	flag.StringVar(&conf, "c", "config.json", "config file path")
	flag.Parse()

	if err := getConfig(conf, &fclient); err != nil {
		log.Fatalf("get config error: %s", err)
	}
	log.Printf("use config: %s", fclient)

	var err error
	localReply, err = buildReply(fclient.Local)
	if err != nil {
		log.Fatalf("fakio start error: %s", err)
	}

	run(fclient.Local)
}
