print("bins_x86[256] = {")
print("// 1 unsorted bin + 62 small bins (total:63)")
print("  fd_unsb, bk_unsb,")
for i in range(2, 64):
  print(f'  fd_{i*8}, bk_{i*8},')

x = 512
print("// 32 large bins of size 64 bytes")
for i in range(1, 32+1):
  print(f'  fd_{x}, bk_{x},')
  x = x + 64

print("// 16 large bins of size 512 bytes")
for i in range(1, 16+1):
  print(f'  fd_{x}, bk_{x},')
  x = x + 512

print("// 8 large bins of size 4096 bytes")
for i in range(1, 8+1):
  print(f'  fd_{x}, bk_{x},')
  x = x + 4096

print("// 4 large bins of size 32768 bytes")
for i in range(1, 4+1):
  print(f'  fd_{x}, bk_{x},')
  x = x + 32768

print("// 2 large bins of size 262144 bytes")
for i in range(1, 2+1):
  print(f'  fd_{x}, bk_{x},')
  x = x + 262144

print("// 1 large bin of whatever size left")
print(f'  fd_{x}, bk_{x},')
print("}")
