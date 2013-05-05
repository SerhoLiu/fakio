## Fakio

一个基于 Socks5 协议，类似 [Shadowsocks][1] 的代理。包括服务端和客户端，可以在客户端和服务端间安全的传输数据。

### 使用方法

#### For Linux

> 1. git clone git://github.com/SerhoLiu/fakio.git
> 2. make
> 3. 在配置文件中添加配置信息
> 4. On server: ./fakio-server path/your/fakio.conf
> 5. On local:  ./fakio_local  path/your/fakio.conf
> 6. 使用支持 Socks5 的服务

#### 使用技巧

不支持守护进程方式，所以挂掉过后不能重新启动，可以使用 [Supervisord][2] 来管理，配置可以参考 supervisord.conf。

### 注意事项
> 1. 协议和 Shadowsocks 有所差异，所以不兼容其客户端
> 2. IPv4 Only
> 3. Linux Only
> 4. 现在还不太稳定，谨慎使用!!!!


## License

MIT LICENSE, see MIT-LICENSE.txt

[1]: https://github.com/clowwindy/shadowsocks
[2]: http://supervisord.org/
