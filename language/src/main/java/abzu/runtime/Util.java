package abzu.runtime;

final class Util {
  private Util() {}

  static int varIntLen(int value) {
    for (int i = 1; i < 5; i++) {
      if (((~0 << (7 * i)) & value) == 0) return i;
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
}
