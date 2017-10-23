import sys

bytes_unread = 0

f = open("/dev/ccpkp", "r+")
while True:
    s = raw_input("> ").strip()
    if (s == "quit"):
        break
    elif (s == "read"):
        print f.read(bytes_unread)
        bytes_unread = 0
    else:
        f.write(s)
        f.flush()
        bytes_unread += len(s)
f.close()
