package abzu.ast.builtin;

import abzu.AbzuException;
import abzu.runtime.NativeObject;
import abzu.runtime.Tuple;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.io.IOException;
import java.nio.channels.AsynchronousFileChannel;
import java.nio.file.OpenOption;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.List;

@NodeInfo(shortName = "fopen")
public abstract class FileOpenNode extends BuiltinNode {
  @Specialization
  public Object fopen(String uri, String mode) {
    try {
      List<OpenOption> openOptions = new ArrayList<>();
      if (mode.equals("r")) {
        openOptions.add(StandardOpenOption.READ);
      }

      AsynchronousFileChannel asynchronousFileChannel = AsynchronousFileChannel.open(Paths.get(uri), openOptions.toArray(new OpenOption[]{}));

      return new Tuple(new NativeObject(asynchronousFileChannel), null, 0l);
    } catch (IOException e) {
      throw new AbzuException(e.getMessage(), this);
    }
  }
}
