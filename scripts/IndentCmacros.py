#!/usr/bin/python3

import sys

if(len(sys.argv))<2:
  print("Usage: "+sys.argv[0]+" <file>", file=sys.stderr)
  sys.exit(1)
with open(sys.argv[1], 'rt') as h:
  lines=h.readlines();

indent=0
stack=[]
for n in range(0, len(lines)):
  trimmedline=lines[n].strip()
  if len(trimmedline) and trimmedline[0]=='#':
    trimmedline=trimmedline[1:].strip()
    command=trimmedline.split()[0]
    thiscomment=''
    if command=='endif' or command=='elif':
      indent-=1
      thiscomment=stack.pop()
      if command=='elif':
        thiscomment=''
    if command=='else':
      thiscomment=stack[-1]
    lines[n]='#'+(''.ljust(indent))+trimmedline+thiscomment+'\n'
    if command[0:2]=='if' or command=='elif':
      indent+=1
      if command=='ifdef':
        stack.append('  /* defined('+trimmedline.split()[1]+') */')
      elif command=='ifndef':
        stack.append('  /* !defined('+trimmedline.split()[1]+') */')
      else:
        stack.append('  /* '+' '.join(trimmedline.split()[1:])+' */')

with open(sys.argv[1], 'wt') as h:
  h.writelines(lines)
  