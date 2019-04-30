package abzu.runtime.async;

import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.TruffleObject;

import java.util.concurrent.CompletableFuture;

@MessageResolution(receiverType = AbzuFuture.class)
public class AbzuFuture implements TruffleObject {

  public CompletableFuture completableFuture;

  public AbzuFuture() {
    this.completableFuture = new CompletableFuture();
  }

  public AbzuFuture(CompletableFuture completableFuture) {
    this.completableFuture = completableFuture;
  }

  @Override
  public ForeignAccess getForeignAccess() {
    return AbzuFutureForeign.ACCESS;
  }

  static boolean isInstance(TruffleObject abzuFuture) {
    return abzuFuture instanceof AbzuFuture;
  }
}
