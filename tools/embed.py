"""Write files as C arrays into one header, for payloads that carry them.

Usage: python embed.py <out.h> <name>=<file> [<name>=<file> ...]
"""
import sys

with open(sys.argv[1], 'w', newline='\n') as out:
    for arg in sys.argv[2:]:
        name, path = arg.split('=', 1)
        with open(path, 'rb') as f:
            data = f.read()
        out.write('static const unsigned char %s[] = {\n' % name)
        for i in range(0, len(data), 16):
            out.write('  ' + ''.join('0x%02x,' % b for b in data[i:i + 16]) + '\n')
        out.write('};\n')
