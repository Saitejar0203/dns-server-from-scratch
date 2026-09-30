"""Integration checks using only Python standard-library UDP sockets."""
import os
import socket,struct,subprocess,time,sys
p=subprocess.Popen([os.environ.get('DNS_BINARY', './build/dns-server')]);s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.settimeout(.2)
def ask(q,count=1):
 packet=struct.pack('!6H',321,256,count,0,0,0)+q
 for _ in range(30):
  s.sendto(packet,('127.0.0.1',2053))
  try:return s.recv(4096)
  except TimeoutError:pass
 raise AssertionError('no response')
try:
 name=b'\x03www\x07example\x03com\0';q=name+b'\0\1\0\1'
 r=ask(q);assert r[12:12+len(q)]==q;assert struct.unpack('!6H',r[:12])==(321,0x8100,1,1,0,0)
 if len(sys.argv)>1:
  q2=b'\x04mail\xc0\x10\0\1\0\1';expanded=b'\x04mail\x07example\x03com\0\0\1\0\1'
  r=ask(q+q2,2);assert r[12:12+len(q)+len(expanded)]==q+expanded
  for invalid in [b'\xc0\x0c',b'\xc0\xff',b'\xc0',b'\x40a']:
   s.sendto(struct.pack('!6H',5,0,1,0,0,0)+invalid,('127.0.0.1',2053))
   try:s.recv(4096);raise AssertionError('malformed packet answered')
   except TimeoutError:pass
  assert ask(q)[-4:]==b'\x08'*4
 print('Variable-name and optional compression/malformed DNS checks passed')
finally:s.close();p.terminate();p.wait()
