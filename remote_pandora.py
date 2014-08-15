import os
import subprocess
import xml.parsers.expat
import sys
import datetime
import serial
from threading import Thread
from Queue import Queue, Empty


ON_POSIX = 'posix' in sys.builtin_module_names



def enqueue_output(out, queue):
    for line in iter(out.readline, b''):
        queue.put(line)
    out.close()



def readLine():
    try:
        line = q.get_nowait()
    except Empty:
        return ''
    else:
        return line



def PlayPandora():
    if pandora_on == False :
        os.system('sudo ./speech.sh Playing Pandora')
        global p
        p = subprocess.Popen('pianobar', stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        #p.stdin.write('13\n')
        global pandora_on
        pandora_on = True
        global q
        q = Queue()
	for iqq in range(6):
	    p.stdout.readline()  ## get these out of the way first
        global t
        t = Thread(target=enqueue_output, args=(p.stdout, q))
	t.daemon = True
        t.start()


def KillPandora():
    p.kill()
    global pandora_on
    pandora_on = False
    os.system('sudo ./speech.sh Good bye, John Fox')

def TalkToPandora(entry):
    print entry
    if entry == '+':
        p.stdin.write(')')
    elif entry == '-':
        p.stdin.write('(')
    elif entry == '^':
        p.stdin.write('n')
    elif entry == 'm':
        p.stdin.write('p')
    elif entry == 'v':
        p.stdin.write('-')
    elif entry == '00':
        while readLine() != '':
            continue
        p.stdin.write('p')
        p.stdin.write('s')
        counter = 0
        while True:
            line = readLine()
            if line == '':
                break
            station = line[line.rfind('  ') + 2 : line.rfind('\n')]
            os.system('sudo ./speech.sh '+ station + '.  Number ' + str(counter))
            counter = counter + 1
        os.system('sudo ./speech.sh Please enter a station number')
    elif entry.isdigit() :
        p.stdin.write('\ns')
        p.stdin.write(entry + '\n')  ##I don't know if this is the right thing to do
  


if __name__ == '__main__':
    print "Remote Pandora Running"
    os.system("sudo ./speech.sh Remote Pandora Running")
    bool_cont = True
    global pandora_on
    pandora_on = False
  
    port = serial.Serial("/dev/ttyAMA0", baudrate=9600, timeout=3.0)  ##maybe increase the timeout

    buffer = ''
  
    while bool_cont :
        rcv = port.read()
        print 'rcv = ' + rcv
        if rcv == 'p':
            if pandora_on :
		KillPandora()
	    else :
		PlayPandora()
	elif rcv.isdigit() :
	    buffer = buffer + rcv
            print 'buffer = ' + buffer
	elif rcv == '\r' :  ## Make \n afte I make the micro-controller just send a '\n' for Enter, rather than "\r\n"
	    TalkToPandora(buffer)
	    buffer = ''
	elif rcv == 'c' :
	    buffer = ''
	elif not rcv == '' :
	    TalkToPandora(rcv)
	    if not buffer == '' :
	        buffer = ''
		
  
 
   
