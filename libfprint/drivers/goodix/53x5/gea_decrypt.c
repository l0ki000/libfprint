void gea_decrypt(uint key,ushort *input_buf,ushort *output_buf,uint size)

{
  ushort uVar1;
  uint uVar2;
  ulonglong output_buf_idx;
  ushort *output_buf_iter;
  uint uVar3;
  
  _g_gea_round_key = key;
  if (size != 0) {
    output_buf_idx = (ulonglong)size;
    output_buf_iter = output_buf;
    do {
      uVar3 = key >> 1 ^ key;
      uVar2 = (((((((key >> 0xf & 0x2000 | key & 0x1000000) >> 1 | key & 0x20000) >> 2 |
                  key & 0x1000) >> 3 | (key >> 7 ^ key) & 0x80000) >> 1 |
                (key >> 0xf ^ key) & 0x4000) >> 2 | key & 0x2000) >> 2 | uVar3 & 0x40 | key & 0x20)
              >> 1 | (key >> 9 ^ key << 8) & 0x800 | (key >> 0x14 ^ key * 2) & 4 |
              (key * 8 ^ _g_gea_round_key >> 0x10) & 0x4000 |
              (key >> 2 ^ _g_gea_round_key >> 0x10) & 0x80 | (key << 6 ^ key >> 7) & 0x100 |
              (key & 0x100) << 7;
      uVar1 = (ushort)key;
      key = (key ^ (uVar3 >> 0x14 ^ key) >> 10) << 0x1f | key >> 1;
      _g_gea_round_key = key;
      *output_buf_iter =
           (short)(uVar2 >> 8) + ((ushort)uVar2 & 0xff | uVar1 & 1) * 0x100 ^
           *(ushort *)
            ((longlong)input_buf + (-2 - (longlong)output_buf) + (longlong)(output_buf_iter + 1));
      output_buf_idx = output_buf_idx - 1;
      output_buf_iter = output_buf_iter + 1;
    } while (output_buf_idx != 0);
  }
  return;
}
