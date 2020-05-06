package yatta;

import java.util.Collections;
import java.util.List;
import java.util.Map;

public final class TestUtil {
  public static final Map<String, Long> testMap() {
    return Collections.singletonMap("one", 1l);
  }

  public static final List<String> testList() {
    return Collections.singletonList("one");
  }

  public static final long mapSize(Map map) {
    return map.size();
  }

  public static final long arraySize(Object[] array) {
    return array.length;
  }
}
