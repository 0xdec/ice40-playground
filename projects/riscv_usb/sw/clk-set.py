#!/usr/bin/env python3

import sys

import usb.core
import usb.util


class iCE1cle(object):

	def __init__(self):
		self.dev = usb.core.find(idVendor=0x1d50, idProduct=0x6145)

	def clk_get(self, idx):
		if idx not in [0, 1]:
			raise ValueError('Invalid clock register')

		vb = self.dev.ctrl_transfer(0xc0, 1, 0, idx, 2)
		val = int.from_bytes(vb, 'little')

		val -= 2048

		return val
	
	def clk_set(self, idx, val):
		if idx not in [0, 1]:
			raise ValueError('Invalid clock register')
		if not (-2048 <= val < 2048):
			raise ValueError('Invalid clock tune value')

		val += 2048

		self.dev.ctrl_transfer(0x40, 0, val, idx)



def main(argv0, val_hi=None, val_lo=None):
	dev = iCE1cle()

	if val_hi is None:
		clk_hi = dev.clk_get(0)
		clk_lo = dev.clk_get(1)
		print(f"{clk_hi:d}/{clk_lo:d}")
	else:
		dev.clk_set(0, int(val_hi))
		if val_lo is not None:
			dev.clk_set(1, int(val_lo))

	return 0


if __name__ == '__main__':
	sys.exit(main(*sys.argv))
