package yatta.ast;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.ReportPolymorphism;
import com.oracle.truffle.api.dsl.TypeSystemReference;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.instrumentation.*;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.oracle.truffle.api.source.SourceSection;
import yatta.Types;
import yatta.TypesGen;
import yatta.runtime.*;

import java.io.File;

@TypeSystemReference(Types.class)
@NodeInfo(language = "yatta", description = "The abstract base node for all strings")
@GenerateWrapper
@ReportPolymorphism
public abstract class ExpressionNode extends Node implements InstrumentableNode {
  private static final int NO_SOURCE = -1;
  private static final int UNAVAILABLE_SOURCE = -2;

  private SourceSection sourceSection;

  private boolean hasRootTag;

  @CompilerDirectives.CompilationFinal
  private boolean isTail = false;

  public boolean isTail() {
    return this.isTail;
  }

  public void setIsTail(boolean isTail) {
    this.isTail = isTail;
  }

  /*
   * The creation of source section can be implemented lazily by looking up the root node source
   * and then creating the source section object using the indices stored in the node. This avoids
   * the eager creation of source section objects during parsing and creates them only when they
   * are needed. Alternatively, if the language uses source sections to implement language
   * semantics, then it might be more efficient to eagerly create source sections and store it in
   * the AST.
   *
   * For more details see {@link InstrumentableNode}.
   */
  @Override
  @CompilerDirectives.TruffleBoundary
  public final SourceSection getSourceSection() {
    return sourceSection;
  }

  public final boolean hasSource() {
    return sourceSection.isAvailable();
  }

  public final boolean isInstrumentable() {
    return hasSource();
  }

  // invoked by the parser to set the source
  public final void setSourceSection(SourceSection sourceSection) {
    this.sourceSection = sourceSection;
  }

  /**
   * Marks this node as being a {@link StandardTags.RootTag} for instrumentation purposes.
   */
  public final void addRootTag() {
    hasRootTag = true;
  }

  @Override
  public String toString() {
    return formatSourceSection(this);
  }

  /**
   * Formats a source section of a node in human readable form. If no source section could be
   * found it looks up the parent hierarchy until it finds a source section. Nodes where this was
   * required append a <code>'~'</code> at the end.
   *
   * @param node the node to format.
   * @return a formatted source section string
   */
  public static String formatSourceSection(Node node) {
    if (node == null) {
      return "<unknown>";
    }
    SourceSection section = node.getSourceSection();
    boolean estimated = false;
    if (section == null) {
      section = node.getEncapsulatingSourceSection();
      estimated = true;
    }

    if (section == null || section.getSource() == null) {
      return "<unknown source>";
    } else {
      String sourceName = new File(section.getSource().getName()).getName();
      int startLine = section.getStartLine();
      return String.format("%s:%d%s", sourceName, startLine, estimated ? "~" : "");
    }
  }

  /**
   * The execute method when no specialization is possible. This is the most general case,
   * therefore it must be provided by all subclasses.
   */
  public abstract Object executeGeneric(VirtualFrame frame);

  @Override
  public WrapperNode createWrapper(ProbeNode probe) {
    return new ExpressionNodeWrapper(this, probe);
  }

  @Override
  public boolean hasTag(Class<? extends Tag> tag) {
    if (tag == StandardTags.ExpressionTag.class) {
      return true;
    }
    if (tag == StandardTags.RootTag.class) {
      return hasRootTag;
    }
    return false;
  }

  /*
   * Execute methods for specialized types. They all follow the same pattern: they call the
   * generic execution method and then expect a result of their return type. Type-specialized
   * subclasses overwrite the appropriate methods.
   */


  public long executeLong(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectLong(executeGeneric(frame));
  }

  public byte executeByte(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectByte(executeGeneric(frame));
  }

  public double executeDouble(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectDouble(executeGeneric(frame));
  }

  public boolean executeBoolean(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectBoolean(executeGeneric(frame));
  }

  public String executeString(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectString(executeGeneric(frame));
  }

  public Function executeFunction(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectFunction(executeGeneric(frame));
  }

  public Unit executeUnit(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectUnit(executeGeneric(frame));
  }

  public Tuple executeTuple(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectTuple(executeGeneric(frame));
  }

  public YattaModule executeModule(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectYattaModule(executeGeneric(frame));
  }

  public StringList executeStringList(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectStringList(executeGeneric(frame));
  }

  public Seq executeSequence(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectSeq(executeGeneric(frame));
  }

  public Dict executeDictionary(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectDict(executeGeneric(frame));
  }

  public Symbol executeSymbol(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectSymbol(executeGeneric(frame));
  }

  public int executeInteger(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectInteger(executeGeneric(frame));
  }

  public Set executeSet(VirtualFrame frame) throws UnexpectedResultException {
    return TypesGen.expectSet(executeGeneric(frame));
  }

  protected boolean isForeignObject(Object obj) {
    return TypesGen.isForeignObject(obj);
  }

  protected boolean isThrowable(Object obj) {
    return obj instanceof Throwable;
  }
}
