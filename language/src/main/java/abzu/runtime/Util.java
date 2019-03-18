package abzu.runtime;

final class Util {
  private Util() {}

  static int varIntLen(int value) {
    for (int i = 1; i < 5; i++) {
      if (((0xffffffff << (7 * i)) & value) == 0) return i;
    }
    return 5;
  }

  static void varIntWrite(int value, byte[] destination, int offset) {
    while ((0xffffff80 & value) != 0) {
      destination[offset++] = (byte) ((0x7f & value) | 0x80);
      value >>>= 7;
    }
    destination[offset] = (byte) value;
  }

  static int varIntRead(byte[] source, int offset) {
    int result = 0;
    byte piece;
    for (int shift = 0; shift <= 28; shift += 7) {
      piece = source[offset++];
      result |= (0x7f & piece) << shift;
      if ((0x80 & piece) == 0) return result;
    }
    return -1;
  }

  static int codePointAt(byte[] array, int offset) {
    final byte b0 = array[offset++];
    if ((0x80 & b0) == 0) return b0;
    final byte b1 = array[offset++];
    if ((0xe0 & b0) == 0xc0) return ((0x1f & b0) << 6) | (0x3f & b1);
    final byte b2 = array[offset++];
    if ((0xf0 & b0) == 0xe0) return ((0x0f & b0) << 12) | ((0x3f & b1) << 6) | (0x3f & b2);
    final byte b3 = array[offset];
    return ((0x7 & b0) << 18) | ((0x3f & b1) << 12) | ((0x3f & b2) << 6) | (0x3f & b3);
  }

  static int codePointLen(int codePoint) {
    if (codePoint < 0x80) return 1;
    if (codePoint < 0x800) return 2;
    if (codePoint < 0x10000) return 3;
    if (codePoint < 0x10ffff) return 4;
    throw new AssertionError();
  }
}
