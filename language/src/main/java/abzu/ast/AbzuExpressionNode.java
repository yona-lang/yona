package abzu.ast;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.ReportPolymorphism;
import com.oracle.truffle.api.dsl.TypeSystemReference;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.instrumentation.*;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.RootNode;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import abzu.AbzuTypes;
import abzu.AbzuTypesGen;

import java.io.File;

@TypeSystemReference(AbzuTypes.class)
@NodeInfo(language = "abzu", description = "The abstract base node for all expressions")
@GenerateWrapper
@ReportPolymorphism
public abstract class AbzuExpressionNode extends Node implements InstrumentableNode {
  private static final int NO_SOURCE = -1;
  private static final int UNAVAILABLE_SOURCE = -2;

  private int sourceCharIndex = NO_SOURCE;
  private int sourceLength;

  private boolean hasRootTag;

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
    if (sourceCharIndex == NO_SOURCE) {
      // AST node without source
      return null;
    }
    RootNode rootNode = getRootNode();
    if (rootNode == null) {
      // not yet adopted yet
      return null;
    }
    SourceSection rootSourceSection = rootNode.getSourceSection();
    if (rootSourceSection == null) {
      return null;
    }
    Source source = rootSourceSection.getSource();
    if (sourceCharIndex == UNAVAILABLE_SOURCE) {
      return source.createUnavailableSection();
    } else {
      return source.createSection(sourceCharIndex, sourceLength);
    }
  }

  public final boolean hasSource() {
    return sourceCharIndex != NO_SOURCE;
  }

  public final boolean isInstrumentable() {
    return hasSource();
  }

  // invoked by the parser to set the source
  public final void setSourceSection(int charIndex, int length) {
    assert sourceCharIndex == NO_SOURCE : "source must only be set once";
    if (charIndex < 0) {
      throw new IllegalArgumentException("charIndex < 0");
    } else if (length < 0) {
      throw new IllegalArgumentException("length < 0");
    }
    this.sourceCharIndex = charIndex;
    this.sourceLength = length;
  }

  public final void setUnavailableSourceSection() {
    assert sourceCharIndex == NO_SOURCE : "source must only be set once";
    this.sourceCharIndex = UNAVAILABLE_SOURCE;
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
    return new AbzuExpressionNodeWrapper(this, probe);
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
    return AbzuTypesGen.expectLong(executeGeneric(frame));
  }

  public boolean executeBoolean(VirtualFrame frame) throws UnexpectedResultException {
    return AbzuTypesGen.expectBoolean(executeGeneric(frame));
  }
}
