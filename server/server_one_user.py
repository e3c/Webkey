#!/usr/bin/python
import socket
import time
import struct
import sys
from threading import Thread
from threading import Condition

connlist = [] # this list will contains the active connections from the phone
lock = Condition() # connlist is protected by this lock
gusername = None # this file works with only one user
grandom = None # the random string to identify the phone
lastknown = None # the last known address of the phone
def appendconn(c):
    lock.acquire()
    try:
        c.setsockopt( socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        c.setsockopt( socket.SOL_TCP, socket.TCP_KEEPCNT, 6)
        c.setsockopt( socket.SOL_TCP, socket.TCP_KEEPIDLE, 180)
        c.setsockopt( socket.SOL_TCP, socket.TCP_KEEPINTVL, 10)
    except:
        print "error in setting up keepalive"
    connlist.append(c)
    if len(connlist) > 3:
        connlist.pop(0).close()
    lock.notifyAll()
    lock.release()
def getconn():
    exptime = time.time()+10
    lock.acquire()
    while len(connlist) == 0:
        try:
            lock.wait(10)
        except RuntimeError:
            lock.release()
            return None
        if exptime < time.time():
            break
    if len(connlist) == 0:
        print "Got signal, but no connection"
        lock.release()
        return None
    c = connlist.pop(0)
    lock.release()
    return c
def closeconns():
    lock.acquire()
    while len(connlist):
        connlist.pop().close()
    lock.notifyAll()
    lock.release()


class connectionthread(Thread):
    def __init__(self,c):
        Thread.__init__(self)
        self.conn = c
    def run(self):
        #print "started phonethread "
        try:
            firstline = self.conn.recv(4096)
        except:
            self.conn.close()
            return
        if firstline.startswith("GET /register_") or firstline.startswith("WEBKEY"):
            self.phoneclient(firstline)
        else:
            self.browserclient(firstline)

    def trysendall(self,data):
        try:
            self.conn.sendall(data)
        except:
            return False
        return True

    def phoneclient(self,firstline):
        global gusername, grandom;
        print "PHONE ", firstline
        p = firstline.find("/")
        if p == -1: conn.close(); return
        if firstline.startswith("GET /register_"):
            p = firstline[14:].find("/")+14
            q = firstline[p+1:].find("/")+p+1
            e = firstline[q+1:].find(" ") + q+1

            username = firstline[14:p]
            random = firstline[p+1:q]
            version = firstline[q+1:e]
            print "register, username = "+username+", random = "+random+", version = "+version+ " " ;
            if username != gusername and grandom != random:
                self.trysendall("HTTP/1.1 200 OK\r\n\r\nUsername is already used.")
                l+="Username is already used. "
            else:
                if not gusernames:
                    gusername = username
                    grandom = random
                self.trysendall("HTTP/1.1 200 OK\r\n\r\nOK")
            self.conn.close()
            return


        username = firstline[7:p]
        q = firstline[p+1:].find("/")+p+1
        g = firstline[q+1:].find("/")+q+1
        e = firstline[g+1:].find("\r") + g+1
        random = firstline[p+1:q]
        version = firstline[q+1:g]
        port = firstline[g+1:e]

        print "username from phone =",username
        if username == gusername  and grandom == random:
            appendconn(self.conn)
        elif not gusername:
            gusername = username
            grandom = random
            appendconn(self.conn)
        else:
            print "Wrong username or random"
            self.trysendall("stop")
            self.conn.close();
            return

    def browserclient(self,firstline):
        print "BROWSER ", firstline
        data = firstline
        data2 = ""
        while 1:
            e = data.find("\r\n\r\n")
            if e != -1:
                data2 = data[e+4:]
                data = data[:e+4]
                break
            dl = len(data)
            try:
                data += self.conn.recv(4096)
            except:
                self.conn.close()
                return
            if dl == len(data):
                #print data
                print "no endline, exiting"
                self.conn.close()
                return;
            #print [ord(s) for s in data[-4:]]
        p = data.find("Content-Length:")
        if p != -1:
            i = p+16
            while i < len(data) and '0' <= data[i] <= '9':
                i+=1;
            cl = int(data[p+16:i])
            cl = min(cl,32*1024*1024) #There is a limit
            while 1:
                dl = len(data2)
                if len(data2) >= cl:
                    break
                try:
                    data2 += self.conn.recv(4096)
                except:
                    return
                if dl == len(data2):
                    self.conn.close()
                    return
        data += data2
        if not data:
            self.conn.close()
            return
        c=None
        while 1:
            c = getconn()
            if c == None:
                self.trysendall("HTTP/1.1 404 Not Found\r\n\r\nPhone is not online\r\n")
                self.conn.close()
                return
            try:
                c.sendall(data)
                break
            except:
                print "sendall error"
                c.close()
        s = 0
        while 1:
            data = None
            try:
                data = c.recv(4096)
                s += len(data)
                if not data: break
                self.trysendall(data)
            except:
                break
        self.conn.close()
        c.close()


HOST = ''
PORT = 8080
if len(sys.argv) > 1:
    try:
        PORT = int(sys.argv[1])
    except:
        pass

clientsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
clientsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
clientsock.bind((HOST, PORT))
clientsock.listen(9500)
print "server started"

while 1:
    try:
        conn, addr = clientsock.accept()
        print 'Connected by', addr
#        conn.setsockopt( socket.SOL_SOCKET, socket.SO_SNDTIMEO, struct.pack('ii',30,0))
        connectionthread(conn).start()
        #print i
    except KeyboardInterrupt:
        try:
            f.close()
        except:
            pass
        break
    except socket.error:
        pass

print "stopping"

clientsock.shutdown(socket.SHUT_RDWR)
clientsock.close()
closeconns()
