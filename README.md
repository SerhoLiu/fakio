## Fakio

一个基于 Socks5 协议，类似 [Shadowsocks][1] 的代理服务端。通过简单的加密协议，可以在一定程度上防止外界对传输内容的干扰等。

Fakio 由运行在 Linux 系统下的服务端和其它平台的客户端组成，本地客户端将本地数据按照[自有协议][2]传输到服务端，服务端将处理请求，并将请求结果返回给本地客户端。其中本地客户端和服务端之间的通信是经过加密的。服务端和客户端通过用户预留密钥进行握手，内容加密密钥采用随机生成，服务端支持多用户。

### 使用方法

理论上，Fakio 服务端可以运行在类 Unix 系统上，不过现在主要还是对 Linux 支持比较好。

#### 服务端 (Linux)

> 1. git clone git://github.com/SerhoLiu/fakio.git
> 2. make fakio-server
> 3. 参照 doc/fakio.conf 进行配置
> 4. On server: ./fakio-server path/your/fakio.conf

不支持守护进程方式，为了实现开机自启动和挂掉后重新启动，可以使用 [Supervisord][3] 来管理，配置可以参考 `tools/supervisord.conf`。

#### 客户端

1. 现在实现了 Linux 系统下的客户端，`make fakio-client` 即可，配置文件见 `clients/config.conf`，客户端通过 SOCKS5 协议和本地程序进行通信。

2. 其它系统客户端计划中

### 注意事项
> 1. 协议和 Shadowsocks 有所差异，所以不兼容其客户端
> 2. IPv4 Only
> 3. 现在还不太稳定，谨慎使用!!!!


## License

MIT LICENSE, see MIT-LICENSE.txt

[1]: https://github.com/clowwindy/shadowsocks
[2]: https://github.com/SerhoLiu/fakio/blob/master/docs/protocol.txt
[3]: http://supervisord.org/
