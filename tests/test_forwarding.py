"""Integration checks using only Python standard-library UDP sockets."""
import os
import socket,struct,subprocess,time,threading
up=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);up.bind(('127.0.0.1',0));up.settimeout(10)
seen=[];errors=[]
def resolver():
 try:
  for ip in [b'\x01\x02\x03\x04',b'\x05\x06\x07\x08']:
   q,addr=up.recvfrom(4096);h=struct.unpack('!6H',q[:12]);assert h[2:]==(1,0,0,0)
   seen.append(q[12:]);r=struct.pack('!6H',h[0],0x8180,1,1,0,0)+q[12:]+b'\xc0\x0c'+struct.pack('!HHIH',1,1,90,4)+ip
   up.sendto(r,addr)
 except Exception as e:errors.append(e)
thread=threading.Thread(target=resolver);thread.start()
p=subprocess.Popen([os.environ.get('DNS_BINARY', './build/dns-server'),'--resolver',f'127.0.0.1:{up.getsockname()[1]}'])
try:
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
  probe.settimeout(.1)
  for attempt in range(100):
   probe.sendto(struct.pack('!6H',0,0,0,0,0,0),('127.0.0.1',2053))
   try:probe.recv(4096);break
   except TimeoutError:pass
  else:raise AssertionError('server did not become ready')
 n1=b'\x03www\x07example\x03com\0';n2=b'\x04mail\x07example\x03com\0';tail=b'\0\1\0\1'
 packet=struct.pack('!6H',777,256,2,0,0,0)+n1+tail+b'\x04mail\xc0\x10'+tail
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as c:
  c.settimeout(5);c.sendto(packet,('127.0.0.1',2053));r=c.recv(4096)
 thread.join();assert not errors,errors
 assert seen==[n1+tail,n2+tail],seen
 assert struct.unpack('!6H',r[:12])==(777,0x8100,2,2,0,0)
 expected=struct.pack('!6H',777,0x8100,2,2,0,0)+n1+tail+n2+tail
 expected+=n1+struct.pack('!HHIH',1,1,90,4)+b'\x01\x02\x03\x04'
 expected+=n2+struct.pack('!HHIH',1,1,90,4)+b'\x05\x06\x07\x08'
 assert r==expected,(r,expected)
 print('Forwarding split queries, compressed upstream answers, TTL, addresses, and merged ID/counts passed')
finally:p.terminate();p.wait();up.close();thread.join()
