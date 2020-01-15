package yatta.runtime.threading;

abstract class CursorWrite extends CursorRead {

  abstract void writeOrdered(long value);

}
