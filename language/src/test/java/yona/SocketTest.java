package yona;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

import java.io.File;
import java.io.IOException;

@ExtendWith(LoggingExtension.class)
public class SocketTest {
  @Test
  public void echoServerTest() throws IOException, InterruptedException {
    int result = new ProcessBuilder("pwsh", "-l", "run.ps1").directory(new File("../tests/echo-server")).start().waitFor();
    assert 0 == result;
  }
}
