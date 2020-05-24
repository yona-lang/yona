package yatta.ast.expression;

import org.junit.jupiter.api.Test;
import yatta.ast.ExpressionNode;
import yatta.ast.StringPartsNode;
import yatta.ast.call.ModuleCallNode;
import yatta.ast.expression.value.*;
import yatta.ast.pattern.ValueMatchNode;
import yatta.runtime.Seq;
import yatta.runtime.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class PatternLetNodeTest {
  @Test
  public void dependencyResolverTest() {
    // This is tests/ZipKeysWithValues.yatta
    Seq aliasNodes = Seq.sequence(
        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "keys_file", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "open", new ExpressionNode[]{new StringPartsNode(new ExpressionNode[]{new StringNode("tests/Keys.txt")}), new SetNode(new ExpressionNode[]{new SymbolNode("read")})}, null)),
        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "values_file", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "open", new ExpressionNode[]{new StringPartsNode(new ExpressionNode[]{new StringNode("tests/Values.txt")}), new SetNode(new ExpressionNode[]{new SymbolNode("read")})}, null)),
        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "keys", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "read_lines", new ExpressionNode[]{new IdentifierNode(null, "keys_file", null)}, null)),
        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "values", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "read_lines", new ExpressionNode[]{new IdentifierNode(null, "values_file", null)}, null)),
        new PatternAliasNode(new ValueMatchNode(new UnitNode()), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "close", new ExpressionNode[]{new IdentifierNode(null, "keys_file", null)}, null)),
        new PatternAliasNode(new ValueMatchNode(new UnitNode()), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "close", new ExpressionNode[]{new IdentifierNode(null, "values_file", null)}, null))
    );
    Set globallyProvidedIdentifiers = Set.set("never", "read", "sleep", "readln", "timeout", "str", "async", "identity", "eval", "float", "int", "println");

    PatternLetNode.AliasTree aliasTree = PatternLetNode.resolveDependencies(aliasNodes, globallyProvidedIdentifiers);

    PatternLetNode.AliasTree expectedAliasTree =
        new PatternLetNode.AliasTreeBatchNode(
            new PatternLetNode.AliasTreeBatchNode(
                new PatternLetNode.AliasTreeBatchNode(
                    new PatternLetNode.AliasNodeRootNode(globallyProvidedIdentifiers),
                    Seq.sequence(
                        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "keys_file", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "open", new ExpressionNode[]{new StringPartsNode(new ExpressionNode[]{new StringNode("tests/Keys.txt")}), new SetNode(new ExpressionNode[]{new SymbolNode("read")})}, null)),
                        new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "values_file", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "open", new ExpressionNode[]{new StringPartsNode(new ExpressionNode[]{new StringNode("tests/Values.txt")}), new SetNode(new ExpressionNode[]{new SymbolNode("read")})}, null)))
                ),
                Seq.sequence(
                    new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "keys", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "read_lines", new ExpressionNode[]{new IdentifierNode(null, "keys_file", null)}, null)),
                    new PatternAliasNode(new ValueMatchNode(new IdentifierNode(null, "values", null)), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "read_lines", new ExpressionNode[]{new IdentifierNode(null, "values_file", null)}, null))
                )),
            Seq.sequence(
                new PatternAliasNode(new ValueMatchNode(new UnitNode()), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "close", new ExpressionNode[]{new IdentifierNode(null, "keys_file", null)}, null)),
                new PatternAliasNode(new ValueMatchNode(new UnitNode()), new ModuleCallNode(null, new FQNNode(new String[0], "File"), "close", new ExpressionNode[]{new IdentifierNode(null, "values_file", null)}, null))
            ));

    assertEquals(expectedAliasTree.toString(), aliasTree.toString());
  }
}
