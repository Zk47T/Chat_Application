# Chat_Application
My chat application, build in Linux to connect through TCP/IP

How to use

## First type 'make' to build the code
```
gcc -g -o main main.c -lpthread
```

## Next you choose a port to listen connection, run like './chat 5000'
```
./chat 5000
```
In another computer with different IP also run like './chat 7000'
```
./chat 7000
```

## Now you can type 'help' to show all the command 
```
help
myip
myport
connect <destination> <port no>
list
terminate <connection id.>
send <connection id.> <message>
exit
```
##You can check IP by type comman 'myip'
```
My IP: 192.168.1.100
```
After that you can connect from ./chat 5000 like

```
connect 192.168.1.100 5000
```



