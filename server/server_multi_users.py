#!/usr/bin/python
import json
import socket
import struct
import sys
import time
from threading import Condition
from threading import Lock
from threading import Thread


class user:
    def __init__(self):
        self.random = ''
        self.connlist = []
        self.lock = Condition()

    def closeconns(self):
        self.lock.acquire()
        while len(self.connlist):
            self.connlist.pop().close()
        self.lock.notifyAll()
        self.lock.release()

    def appendconn(self, c):
        self.lock.acquire()
        try:
            c.setsockopt( socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            c.setsockopt( socket.SOL_TCP, socket.TCP_KEEPCNT, 6)
            c.setsockopt( socket.SOL_TCP, socket.TCP_KEEPIDLE, 180)
            c.setsockopt( socket.SOL_TCP, socket.TCP_KEEPINTVL, 10)
        except Exception as e:
            print "error in setting up keepalive ", e
        self.connlist.append(c)
        if len(self.connlist) > 3:
            self.connlist.pop(0).close()
        self.lock.notifyAll()
        self.lock.release()

    def getconn(self):
        exptime = time.time()+10
        self.lock.acquire()
        while len(self.connlist) == 0:
            try:
                self.lock.wait(10)
            except RuntimeError:
                self.lock.release()
                return None
            if exptime < time.time():
                break
        if len(self.connlist) == 0:
            print "Got signal, but no connection"
            self.lock.release()
            return None
        c = self.connlist.pop(0)
        self.lock.release()
        return c

class userlist:
    def __init__(self):
        self.users = {}
        self.connlist = [] # this list will contains the active connections from the phone
        self.lock = Condition() # connlist is protected by this lock

    def get(self, name):
        try:
            return self.users[name]
        except:
            pass
        return None

    def add(self, name, random):
        print "Adding new user: ", name, " random: ", random
        u = user()
        u.random = random
        self.users[name] = u
        Persistence.save()


class Persistence():
    lock = Lock()

    @classmethod
    def save(cls):
        cls.lock.acquire(True)
        try:
            dict_users_random = {}
            for name in users.users:
                dict_users_random[name] = users.users[name].random
            serialized = json.dumps(dict_users_random)
            with open("users", "w") as users_file:
                users_file.write(serialized)
        except Exception as e:
            print "Error saving users"
        cls.lock.release()

    @classmethod
    def load(cls):
        try:
            with open("users", "r") as users_file:
                unserialized = json.load(users_file)
                for name in unserialized:
                    users.add(name, unserialized[name])
        except Exception as e:
            print "Error loading users"


users = userlist()
Persistence.load()


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
        print "PHONE ", firstline
        p = firstline.find("/")
        if p == -1: conn.close(); return
        if firstline.startswith("GET /register_"):
            p = firstline[14:].find("/")+14
            q = firstline[p+1:].find("/")+p+1
            e = firstline[q+1:].find(" ") + q+1

            username = firstline[14:p]
            random = firstline[p+1:q]
            print "register, username = "+username+", random = "+random+ " " ;

            if users.get(username):
                self.trysendall("HTTP/1.1 200 OK\r\n\r\nUsername is already used.")
            else:
                users.add(username, random)
                self.trysendall("HTTP/1.1 200 OK\r\n\r\nOK")
            self.conn.close()
            return


        username = firstline[7:p]
        q = firstline[p+1:].find("/")+p+1
        g = firstline[q+1:].find("/")+q+1
        e = firstline[g+1:].find("\r") + g+1
        random = firstline[p+1:q]
        port = firstline[g+1:e]

        print "username from phone =",username
        if users.get(username) and users.get(username).random == random:
            users.get(username).appendconn(self.conn)
            return
        else:
            print "Wrong username or random"
            self.trysendall("stop")
            self.conn.close();
            return

    def browserclient(self,firstline):
        print "BROWSER ", firstline[:firstline.find("\r\n")]
        u = None
        req = firstline[:firstline.find(' ')]
        print "REQ is", req, len(req)
        p = firstline[len(req)+2:].find("/")
        s = firstline[len(req)+2:].find(" ")
        if s < p:
            p = s
        print "P is ", p
        username = firstline[len(req) + 2:p+len(req)+2]
        print "Username:", username
        u = users.get(username)

        if u == None:
            self.trysendall("HTTP/1.1 404 Not Found\r\n\r\nPhone is unknown\r\n")
            self.conn.close()
            return

        data = req + " " + firstline[p+len(req)+2:]
        print "REWRITTEN as", data[:data.find("\r\n")]
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
            c = u.getconn()
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
for u in userlist:
    u.closeconns
