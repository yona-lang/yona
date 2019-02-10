package abzu.ast.pattern;

import com.oracle.truffle.api.nodes.ControlFlowException;

/**
 * This exception can be thrown when pattern matching on a curried function with zero arguments.
 * That situation can not be detected while parsing, because the parser will recognize symbol as an identifier.
 * At actual pattern matching, NodeMaker will eventually receive a Function, which cannot be translated back to FunctionNode,
 * nor would it make sense, as pattern matching on a function is meaningless.
 *
 * This is an internal exception and should be handled accordingly.
 */
public class CurriedFunctionMatchException extends ControlFlowException {
  public static CurriedFunctionMatchException INSTANCE = new CurriedFunctionMatchException();

  private CurriedFunctionMatchException() {
    super();
  }
}
