package yona.ast.expression;

import org.junit.jupiter.api.Test;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.StringPartsNode;
import yona.ast.call.ModuleCallNode;
import yona.ast.expression.aliasTree.AliasNodeRootNode;
import yona.ast.expression.aliasTree.AliasTreeBatchNode;
import yona.ast.expression.aliasTree.AliasTreeNode;
import yona.ast.expression.value.*;
import yona.ast.pattern.ValueMatchNode;
import yona.runtime.Seq;
import yona.runtime.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class PatternLetNodeTest {
  @Test
  public void dependencyResolverTest() {
    // This is tests/ZipKeysWithValues.yona
    Seq aliasNodes = Seq.sequence(
        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "keys_file", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "open", new ExpressionNode[]{new StringPartsNode(new ExpressionNode[]{new StringNode("tests/Keys.txt")}), new SetNode(new ExpressionNode[]{new SymbolNode("read")})}, null)),
        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "values_file", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "open", new ExpressionNode[]{new StringPartsNode(new ExpressionNode[]{new StringNode("tests/Values.txt")}), new SetNode(new ExpressionNode[]{new SymbolNode("read")})}, null)),
        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "keys", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "read_lines", new ExpressionNode[]{new IdentifierNode(null, "keys_file", null)}, null)),
        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "values", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "read_lines", new ExpressionNode[]{new IdentifierNode(null, "values_file", null)}, null)),
        new PatternAliasNode(new ValueMatchNode(UnitNode.INSTANCE), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "close", new ExpressionNode[]{new IdentifierNode(null, "keys_file", null)}, null)),
        new PatternAliasNode(new ValueMatchNode(UnitNode.INSTANCE), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "close", new ExpressionNode[]{new IdentifierNode(null, "values_file", null)}, null))
    );
    Set globallyProvidedIdentifiers = Set.set("never", "read", "sleep", "readln", "timeout", "str", "async", "identity", "eval", "float", "int", "println");

    AliasTreeNode aliasTreeNode = PatternLetNode.resolveDependencies(aliasNodes, globallyProvidedIdentifiers);

    AliasTreeNode expectedAliasTreeNode =
        new AliasTreeBatchNode(
            new AliasTreeBatchNode(
                new AliasTreeBatchNode(
                    new AliasNodeRootNode(globallyProvidedIdentifiers),
                    new AliasNode[]{
                        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "keys_file", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "open", new ExpressionNode[]{new StringPartsNode(new ExpressionNode[]{new StringNode("tests/Keys.txt")}), new SetNode(new ExpressionNode[]{new SymbolNode("read")})}, null)),
                        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "values_file", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "open", new ExpressionNode[]{new StringPartsNode(new ExpressionNode[]{new StringNode("tests/Values.txt")}), new SetNode(new ExpressionNode[]{new SymbolNode("read")})}, null))
                    }),
            new AliasNode[]{
                new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "keys", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "read_lines", new ExpressionNode[]{new IdentifierNode(null, "keys_file", null)}, null)),
                new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "values", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "read_lines", new ExpressionNode[]{new IdentifierNode(null, "values_file", null)}, null))
            }),
    new AliasNode[]{
        new PatternAliasNode(new ValueMatchNode(UnitNode.INSTANCE), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "close", new ExpressionNode[]{new IdentifierNode(null, "keys_file", null)}, null)),
        new PatternAliasNode(new ValueMatchNode(UnitNode.INSTANCE), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "close", new ExpressionNode[]{new IdentifierNode(null, "values_file", null)}, null))
    });

    assertEquals(expectedAliasTreeNode.toString(), aliasTreeNode.toString());
  }
}
