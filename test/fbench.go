// The fakio server benchmark tool
// from github.com/shadowsocks/shadowsocks-go

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

var N, C int

//var T time.Duration
var URL string

var ErrHandToServer = errors.New("handshake to server error")
var ErrHandFromServer = errors.New("handshake from server error")

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

type Result struct {
	ok  int64
	err int64
	t   time.Duration
}

func doGet(client *http.Client) (t time.Duration, err error) {
	s := time.Now()
	resq, err := client.Get(URL)
	t = time.Now().Sub(s)

	if err == nil {
		resq.Body.Close()
	}
	return
}

func clients(r chan Result, client *http.Client) {
	result := Result{0, 0, 0}

	defer func() {
		r <- result
	}()

	//var oks, errs int64

	for i := 0; i < N; i++ {
		t, err := doGet(client)
		result.t += t
		if err != nil {
			fmt.Println(err)
			result.err += 1
		} else {
			result.ok += 1
		}
	}
}

func run(client *http.Client) {
	results := make([]chan Result, C)

	var okReq, errReq int64
	var allTime time.Duration

	start := time.Now()
	for i := 0; i < C; i++ {
		results[i] = make(chan Result)
		go clients(results[i], client)
	}

	for _, ch := range results {
		req := <-ch
		okReq += req.ok
		errReq += req.err
		allTime += req.t
	}

	total := time.Now().Sub(start)

	fmt.Printf("\nSummary:\n")
	fmt.Printf("        Total Request:\t%d total, %d susceed, %d failed.\n", N*C, okReq, errReq)
	fmt.Printf("           Total time:\t%4.4f secs.\n", total.Seconds())
	fmt.Printf("         Average time:\t%4.4f secs.\n", allTime.Seconds()/float64(C))
	fmt.Printf("     Time per request:\t%4.4f secs.\n", allTime.Seconds()/float64(C))
	fmt.Printf("  Requests per second:\t%4.4f\n", float64(okReq)/total.Seconds())
}

func main() {
	var useProxy bool

	flag.IntVar(&C, "c", 100, "The number of concurrent clients to run")
	flag.IntVar(&N, "n", 100, "Total number of per client requests to make")
	flag.BoolVar((*bool)(&useProxy), "f", false, "use fakio proxy")

	flag.Parse()

	if len(flag.Args()) != 1 {
		fmt.Println("Usage: fbench -f -c clients -n requests url")
		return
	}

	URL = flag.Arg(0)
	if strings.HasPrefix(URL, "https://") {
		fmt.Println("https not supported")
		return
	}
	if !strings.HasPrefix(URL, "http://") {
		URL = "http://" + URL
	}

	parsedURL, err := url.Parse(URL)
	if err != nil {
		fmt.Println("Error parsing url:", err)
		return
	}

	var client *http.Client

	if useProxy {
		host, _, err := net.SplitHostPort(parsedURL.Host)
		if err != nil {
			host = net.JoinHostPort(parsedURL.Host, "80")
		} else {
			host = parsedURL.Host
		}

		tr := &http.Transport{
			Dial: func(_, _ string) (net.Conn, error) {
				return FakioDial(host, TestServer)
			},
		}

		client = &http.Client{Transport: tr}
	} else {
		client = &http.Client{}
	}

	run(client)
}
