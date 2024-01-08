#!/usr/bin/env python3
import sys

def decode_inst(inst):
    if (inst & 0x0ffffff0) == 0x012fff10:   # branch and exchange
        print('Branch and exchange')
    elif (inst & 0x0e000000) == 0x08000000: # block data transfer
        print('Block data transfer')
    elif (inst & 0x0e000000) == 0x0a000000: # branch and branch with link
        print('Branch/branch with link')
    elif (inst & 0x0f000000) == 0x0f000000: # software interrupt
        print('SWI')
    elif (inst & 0x0e000010) == 0x06000010: # undefined
        print('Undefined')
    elif (inst & 0x0c000000) == 0x04000000: # single data transfer
        print('Single data transfer')
    elif (inst & 0x0f800ff0) == 0x01000090: # single data swap
        print('Single data swap')
    elif (inst & 0x0f0000f0) == 0x00000090: # multiply and multiply long
        print('Multiply and multiply long')
    elif (inst & 0x0e400f90) == 0x00000090: # halfword data transfer register
        print('Halfword transfer register')
    elif (inst & 0x0e400090) == 0x00400090: # halfword data transfer immediate
        print('Halfword transfer immediate')
    elif (inst & 0x0fbf0000) == 0x010f0000: # PSR transfer MRS
        print('PSR transfer MRS')
    elif (inst & 0x0db0f000) == 0x0120f000: # PSR transfer MSR
        print('PSR transfer MSR')
    elif (inst & 0x0c000000) == 0x00000000: # data processing
        print('Data processing')
    else:
        print('Illegal instruction')

if __name__ == '__main__':
    if not sys.argv[1:]:
        print(f'Usage: {sys.argv[0]} <instruction>')
        sys.exit(2)

    inst = int(sys.argv[1], 0)
    decode_inst(inst)
