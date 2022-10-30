### 这个是学习tiny_http而来的练手项目~
1.  该项目是基于C++ 实现的HTTP服务器，也就是那种烂大街的WebServe服务器，不过由于是自己动手做的,就丢上来了.
2.  接下来会试试boost/asio的项目进行练手.
3.  克隆完本项目之后,输入指令 `g++  http.cpp -D_REENTRANT~ -o http1 -lpthread `
4.  ./http1 8888
5.  然后访问 127.0.0.1:8888/index.html 即可.