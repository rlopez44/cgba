#!/usr/bin/env python3
import sys

def decode_arm_inst(inst):
    print('ARM:', end=' ')

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

def decode_thumb_inst(inst):
    print('THUMB:', end=' ')

    if (inst & 0xff00) == 0xdf00:   # software interrupt
        print('Software interrupt')
    elif (inst & 0xf800) == 0xe000: # unconditional branch
        print('Unconditional branch')
    elif (inst & 0xf000) == 0xd000: # conditional branch
        print('Conditional branch')
    elif (inst & 0xf000) == 0xc000: # multiple load/store
        print('Multiple load/store')
    elif (inst & 0xf000) == 0xf000: # long branch w/link
        print('Long branch w/link')
    elif (inst & 0xff00) == 0xb000: # add offset to SP
        print('Add offset to SP')
    elif (inst & 0xf600) == 0xb400: # push/pop registers
        print('Push/pop registers')
    elif (inst & 0xf000) == 0x8000: # load/store halfword
        print('Load/store halfword')
    elif (inst & 0xf000) == 0x9000: # SP relative load/store
        print('SP relative load/store')
    elif (inst & 0xf000) == 0xa000: # load address
        print('Load address')
    elif (inst & 0xe000) == 0x6000: # load/store w/immediate offset
        print('Load/store w/immediate offset')
    elif (inst & 0xf200) == 0x5000: # load/store w/register offset
        print('Load/store w/register offset')
    elif (inst & 0xf200) == 0x5200: # load/store sign-extended byte/halfword
        print('Load/store sign-extended byte/halfword')
    elif (inst & 0xf800) == 0x4800: # PC relative load
        print('PC relative load')
    elif (inst & 0xfc00) == 0x4400: # hi register operations/branch exchange
        print('Hi register operations/branch exchange')
    elif (inst & 0xfc00) == 0x4000: # ALU operations
        print('ALU operations')
    elif (inst & 0xe000) == 0x2000: # move/compare/add/subtract immediate
        print('Move/compare/add/subtract immediate')
    elif (inst & 0xf800) == 0x1800: # add/subtract
        print('Add/subtract')
    elif (inst & 0xe000) == 0x0000: # move shifted register
        print('Move shifted register')
    else:
        print('Illegal instruction')

if __name__ == '__main__':
    if not sys.argv[1:]:
        print(f'Usage: {sys.argv[0]} <instruction>', file=sys.stderr)
        sys.exit(2)

    instlen = len(sys.argv[1])
    if instlen not in (4, 8):
        print('Incorrect instruction format. Expected 32-bit or 16-bit hex value',
              file=sys.stderr)
        sys.exit(1)

    inst = int(sys.argv[1], base=16)
    if instlen == 4:
        decode_thumb_inst(inst)
    else:
        decode_arm_inst(inst)
