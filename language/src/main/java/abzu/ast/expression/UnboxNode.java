/*
 * Copyright (c) 2012, 2014, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * The Universal Permissive License (UPL), Version 1.0
 *
 * Subject to the condition set forth below, permission is hereby granted to any
 * person obtaining a copy of this software, associated documentation and/or
 * data (collectively the "Software"), free of charge and under any and all
 * copyright rights in the Software, and any and all patent rights owned or
 * freely licensable by each licensor hereunder covering either (i) the
 * unmodified Software as contributed to or provided by such licensor, or (ii)
 * the Larger Works (as defined below), to deal in both
 *
 * (a) the Software, and
 *
 * (b) any piece of software and/or hardware listed in the lrgrwrks.txt file if
 * one is included with the Software each a "Larger Work" to which the Software
 * is contributed by such licensors),
 *
 * without restriction, including without limitation the rights to copy, create
 * derivative works of, display, perform, and distribute the Software and make,
 * use, sell, offer for sale, import, export, have made, and have sold the
 * Software and the Larger Work(s), and to sublicense the foregoing rights on
 * either these or other terms.
 *
 * This license is subject to the following condition:
 *
 * The above copyright notice and either this complete permission notice or at a
 * minimum a reference to the UPL must be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
package abzu.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Cached;
import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.NodeChild;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.Message;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;
import abzu.AbzuException;
import abzu.ast.AbzuExpressionNode;
import abzu.ast.interop.AbzuForeignToAbzuTypeNode;
import abzu.runtime.AbzuFunction;
import abzu.runtime.AbzuUnit;

@NodeChild("child")
public abstract class UnboxNode extends AbzuExpressionNode {

  @Specialization
  protected long unboxLong(long value) {
    return value;
  }

  @Specialization
  protected boolean unboxBoolean(boolean value) {
    return value;
  }

  @Specialization
  protected String unboxString(String value) {
    return value;
  }

  @Specialization
  protected AbzuFunction unboxFunction(AbzuFunction value) {
    return value;
  }

  @Specialization
  protected AbzuUnit unboxNull(AbzuUnit value) {
    return value;
  }

  @Specialization
  protected ValueNode unboxValue(ValueNode value) {
    return value;
  }

  @Specialization(guards = "isBoxedPrimitive(value)")
  protected Object unboxBoxed(
      Object value,
      @Cached("create()") AbzuForeignToAbzuTypeNode foreignToAbzu) {
    return foreignToAbzu.unbox((TruffleObject) value);
  }

  @Specialization(guards = "!isBoxedPrimitive(value)")
  protected Object unboxGeneric(Object value) {
    return value;
  }

  @Node.Child private Node isBoxed;

  protected boolean isBoxedPrimitive(Object value) {
    if (value instanceof TruffleObject) {
      if (isBoxed == null) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        isBoxed = insert(Message.IS_BOXED.createNode());
      }
      if (ForeignAccess.sendIsBoxed(isBoxed, (TruffleObject) value)) {
        return true;
      }
    }
    return false;
  }

  @Fallback
  protected Object typeError(Object value) {
    throw AbzuException.typeError(this, value);
  }

}
