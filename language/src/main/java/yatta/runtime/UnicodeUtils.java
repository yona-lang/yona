package yatta.runtime;

import java.nio.ByteBuffer;

public final class UnicodeUtils {
  private UnicodeUtils() {}

  public static short int16Read(final byte[] src, int offset) {
    return (short) ((src[offset] << 8) | (src[++offset] & 0xff));
  }

  public static void int16Write(final short val, final byte[] dst, int offset) {
    dst[offset] = (byte) (val >> 8);
    dst[++offset] = (byte) val;
  }

  public static int int32Read(final byte[] src, int offset) {
    int result = 0;
    result |= src[offset] << 24;
    result |= (src[++offset] & 0xff) << 16;
    result |= (src[++offset] & 0xff) <<  8;
    result |= src[++offset] & 0xff;
    return result;
  }

  public static void int32Write(int value, final byte[] dst, int offset) {
    dst[offset] = (byte)(value >> 24);
    dst[++offset] = (byte)(value >> 16);
    dst[++offset] = (byte)(value >> 8);
    dst[++offset] = (byte) value;
  }

  public static long int64Read(final byte[] src, int offset) {
    long result = 0;
    result |= ((long) src[offset]) << 56;
    result |= ((long) src[++offset] & 0xff) << 48;
    result |= ((long) src[++offset] & 0xff) << 40;
    result |= ((long) src[++offset] & 0xff) << 32;
    result |= ((long) src[++offset] & 0xff) << 24;
    result |= ((long) src[++offset] & 0xff) << 16;
    result |= ((long) src[++offset] & 0xff) <<  8;
    result |= (long) src[++offset] & 0xff;
    return result;
  }

  public static void int64Write(long value, final byte[] dst, int offset) {
    dst[offset] = (byte)(value >> 56);
    dst[++offset] = (byte)(value >> 48);
    dst[++offset] = (byte)(value >> 40);
    dst[++offset] = (byte)(value >> 32);
    dst[++offset] = (byte)(value >> 24);
    dst[++offset] = (byte)(value >> 16);
    dst[++offset] = (byte)(value >> 8);
    dst[++offset] = (byte) value;
  }

  public static long varInt63Read(final byte[] source, int offset) {
    long result = 0;
    byte piece;
    for (int shift = 0; shift <= 63; shift += 7) {
      piece = source[offset++];
      result |= (0x7fL & piece) << shift;
      if ((0x80 & piece) == 0) break;
    }
    return result;
  }

  public static int varInt63Len(long value) {
    int result = 1;
    while ((0xffffffffffffff80L & value) != 0) {
      result++;
      value >>>= 7;
    }
    return result;
  }

  public static void varInt63Write(long value, byte[] destination, int offset) {
    while ((0xffffffffffffff80L & value) != 0) {
      destination[offset++] = (byte) ((0x7f & value) | 0x80);
      value >>>= 7;
    }
    destination[offset] = (byte) value;
  }

  public static int utf8Decode(byte[] src, int offset) {
    final byte b0 = src[offset++];
    if ((0x80 & b0) == 0) return b0;
    final byte b1 = src[offset++];
    if ((0xe0 & b0) == 0xc0) return ((0x1f & b0) << 6) | (0x3f & b1);
    final byte b2 = src[offset++];
    if ((0xf0 & b0) == 0xe0) return ((0x0f & b0) << 12) | ((0x3f & b1) << 6) | (0x3f & b2);
    final byte b3 = src[offset];
    return ((0x7 & b0) << 18) | ((0x3f & b1) << 12) | ((0x3f & b2) << 6) | (0x3f & b3);
  }

  public static int utf8Length(int codePoint) {
    if (codePoint < 0x80) return 1;
    if (codePoint < 0x800) return 2;
    if (codePoint < 0xd800) return 3;
    if (codePoint < 0xe000) return -1;
    if (codePoint < 0x10000) return 3;
    if (codePoint < 0x10ffff) return 4;
    return -1;
  }

  public static int utf8Offset(final byte[] bytes, int offset, int idx) {
    while (idx > 0) {
      switch ((0xf0 & bytes[offset]) >>> 4) {
        case 0b0000:
        case 0b0001:
        case 0b0010:
        case 0b0011:
        case 0b0100:
        case 0b0101:
        case 0b0110:
        case 0b0111:
          offset += 1;
          break;
        case 0b1000:
        case 0b1001:
        case 0b1010:
        case 0b1011:
          throw new AssertionError();
        case 0b1100:
        case 0b1101:
          offset += 2;
          break;
        case 0b1110:
          offset += 3;
          break;
        case 0b1111:
          offset += 4;
      }
      idx--;
    }
    return offset;
  }

  public static void utf8Encode(final ByteBuffer dst, int codePoint) {
    switch (utf8Length(codePoint)) {
      case 1: {
        dst.put((byte) codePoint);
        break;
      }
      case 2: {
        dst.put((byte) (0xc0 | (codePoint >> 6)));
        dst.put((byte) (0x80 | (codePoint & 0x3f)));
        break;
      }
      case 3: {
        dst.put((byte) (0xe0 | (codePoint >> 12)));
        dst.put((byte) (0x80 | ((codePoint >> 6) & 0x3f)));
        dst.put((byte) (0x80 | (codePoint & 0x3f)));
        break;
      }
      case 4: {
        dst.put((byte) (0xf0 | (codePoint >> 18)));
        dst.put((byte) (0x80 | ((codePoint >> 12) & 0x3f)));
        dst.put((byte) (0x80 | ((codePoint >>  6) & 0x3f)));
        dst.put((byte) (0x80 | (codePoint & 0x3f)));
        break;
      }
      default: throw new AssertionError();
    }
  }
}
