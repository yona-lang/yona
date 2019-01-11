package abzu.runtime;

import java.util.function.BiFunction;
import java.util.function.Supplier;

public abstract class Sequence {

    public abstract Sequence push(Object value);

    public abstract Sequence inject(Object value);

    public abstract Object first();

    public abstract Object last();

    public abstract Sequence removeFirst();

    public abstract Sequence removeLast();

    public abstract Object foldLeft(BiFunction function, Object initial);

    public abstract Object foldRight(BiFunction function, Object initial);

    public abstract int length();

    public static Sequence catenate(Sequence first, Sequence second) {
        if (first instanceof Deep) {
            if (second instanceof Deep) {
                final Deep left = (Deep) first;
                final Deep right = (Deep) second;
                final Sequence forcedLeftSub = left.forceSub();
                final Sequence forcedRightSub = right.forceSub();
                final int newSubLength = left.subLength + left.suffix.length() + right.prefix.length() + right.subLength;
                Supplier<Sequence> newSub = () -> {
                    Sequence leftSub = forcedLeftSub;
                    //noinspection ConstantConditions
                    for (Node node : makeNodes(elementsOf(left.suffix, right.prefix))) leftSub = leftSub.inject(node);
                    return catenate(leftSub, forcedRightSub);
                };
                return new Deep(left.prefix, newSub, newSubLength, right.suffix);
            } else {
                assert second instanceof Shallow;
                return (Sequence) second.foldLeft((seq, v) -> ((Sequence) seq).inject(v), first);
            }
        } else {
            assert first instanceof Shallow;
            return (Sequence) first.foldRight((seq, v) -> ((Sequence) seq).push(v), second);
        }
    }

    private static Node[] makeNodes(Object[] m) {
        assert m.length >= 2;
        assert m.length < 7;
        switch (m.length) {
            case 2: return (Node[]) new Object[]{ new Two(m[0], measure(m[0]), m[1], measure(m[1])) };
            case 3: return (Node[]) new Object[]{ new Three(m[0], measure(m[0]), m[1], measure(m[1]), m[2], measure(m[2])) };
            case 4: return (Node[]) new Object[]{ new Two(m[0], measure(m[0]), m[1], measure(m[1])), new Two(m[2], measure(m[2]), m[3], measure(m[3])) };
            case 5: return (Node[]) new Object[]{ new Two(m[0], measure(m[0]), m[1], measure(m[1])), new Three(m[2], measure(m[2]), m[3], measure(m[3]), m[4], measure(m[4])) };
            case 6: return (Node[]) new Object[]{ new Three(m[0], measure(m[0]), m[1], measure(m[1]), m[2], measure(m[2])), new Three(m[3], measure(m[3]), m[4], measure(m[4]), m[5], measure(m[5])) };
            default: {
                assert false;
                return null;
            }
        }
    }

    private static Object[] elementsOf(Affix left, Affix right) {
        final Object[] result = new Object[left.length() + right.length()];
        final int[] i = {0};
        left.foldLeft((r, m) -> { ((Object[]) r)[i[0]++] = m; return result; }, result);
        right.foldLeft((r, m) -> { ((Object[]) r)[i[0]++] = m; return result; }, result);
        return result;
    }

    private static int measure(Object o) {
        return o instanceof Node ? ((Node) o).length() : 1;
    }

    private static final class Shallow extends Sequence {
        static final Shallow EMPTY = new Shallow(null);

        final Object value;

        Shallow(Object value) {
            this.value = value;
        }

        @Override
        public Sequence push(Object value) {
            return this.value == null ? new Shallow(value) : new Deep(value, this.value);
        }

        @Override
        public Sequence inject(Object value) {
            return this.value == null ? new Shallow(value) : new Deep(this.value, value);
        }

        @Override
        public Object first() {
            return value;
        }

        @Override
        public Object last() {
            return value;
        }

        @Override
        public Sequence removeFirst() {
            return value == null ? null : EMPTY;
        }

        @Override
        public Sequence removeLast() {
            return value == null ? null : EMPTY;
        }

        @SuppressWarnings("unchecked")
        @Override
        public Object foldLeft(BiFunction function, Object initial) {
            return value == null ? initial : function.apply(initial, value);
        }

        @SuppressWarnings("unchecked")
        @Override
        public Object foldRight(BiFunction function, Object initial) {
            return value == null ? initial : function.apply(initial, value);
        }

        @Override
        public int length() {
            return value == null ? 0 : measure(value);
        }
    }

    private static final class Deep extends Sequence {
        final Affix prefix;
        volatile Object sub;
        final Affix suffix;

        final int subLength;

        Deep(Affix prefix, Object sub, int subLength, Affix suffix) {
            this.prefix = prefix;
            this.sub = sub;
            this.suffix = suffix;
            this.subLength = subLength;
        }

        Deep(Object first, Object second) {
            prefix = new One(first);
            sub = Shallow.EMPTY;
            suffix = new One(second);
            subLength = 0;
        }

        Sequence forceSub() {
            boolean done = (sub instanceof Sequence);
            if (!done) {
                synchronized (this) {
                    done = (sub instanceof Sequence);
                    if (!done) {
                        assert sub instanceof Supplier;
                        sub = ((Supplier) sub).get();
                    }
                }
            }
            return (Sequence) sub;
        }

        @Override
        public Sequence push(Object value) {
            if (!(prefix instanceof Three)) return new Deep(prefix.push(value), sub, subLength, suffix);
            final Sequence forcedSub = forceSub();
            final Supplier<Sequence> newSub = () -> forcedSub.push(prefix);
            return new Deep(new One(value), newSub, subLength + prefix.length(), suffix);
        }

        @Override
        public Sequence inject(Object value) {
            if (!(suffix instanceof Three)) return new Deep(prefix, sub, subLength, suffix.inject(value));
            final Sequence forcedSub = forceSub();
            final Supplier<Sequence> newSub = () -> forcedSub.inject(suffix);
            return new Deep(prefix, newSub, subLength + suffix.length(), new One(value));
        }

        @Override
        public Object first() {
            return prefix.first();
        }

        @Override
        public Object last() {
            return suffix.last();
        }

        @Override
        public Sequence removeFirst() {
            if (prefix instanceof Node) {
                assert prefix.removeFirst() != null;
                return new Deep(prefix.removeFirst(), sub, subLength, suffix);
            } else if (subLength > 0) {
                final Sequence forcedSub = forceSub();
                assert forcedSub.first() instanceof Node;
                final Node first = (Node) forcedSub.first();
                final Supplier<Sequence> newSub = forcedSub::removeFirst;
                return new Deep(first, newSub, subLength - first.length(), suffix);
            } else {
                final Affix newSuffix = suffix.removeFirst();
                return newSuffix == null ? new Shallow(suffix.first()) : new Deep(new One(suffix.first()), Shallow.EMPTY, 0, newSuffix);
            }
        }

        @Override
        public Sequence removeLast() {
            if (suffix instanceof Node) {
                assert suffix.removeLast() != null;
                return new Deep(prefix, sub, subLength, suffix.removeLast());
            } else if (subLength > 0) {
                final Sequence forcedSub = forceSub();
                assert forcedSub.last() instanceof Node;
                final Node last = (Node) forcedSub.last();
                final Supplier<Sequence> newSub = forcedSub::removeLast;
                return new Deep(prefix, newSub, subLength - last.length(), last);
            } else {
                final Affix newPrefix = prefix.removeLast();
                return newPrefix == null ? new Shallow(prefix.last()) : new Deep(newPrefix, Shallow.EMPTY, 0, new One(prefix.last()));
            }
        }

        @SuppressWarnings("unchecked")
        @Override
        public Object foldLeft(BiFunction function, Object initial) {
            Object result = initial;
            result = prefix.foldLeft(function, result);
            result = forceSub().foldLeft((n, node) -> ((Node) node).foldLeft(function, n), result);
            result = suffix.foldLeft(function, result);
            return result;
        }

        @SuppressWarnings("unchecked")
        @Override
        public Object foldRight(BiFunction function, Object initial) {
            Object result = initial;
            result = suffix.foldRight(function, result);
            result = forceSub().foldRight((n, node) -> ((Node) node).foldRight(function, n), result);
            result = prefix.foldRight(function, result);
            return result;
        }

        @Override
        public int length() {
            return prefix.length() + subLength + suffix.length();
        }
    }

    private static abstract class Affix {

        abstract Affix push(Object value);

        abstract Affix inject(Object value);

        abstract Object first();

        abstract Object last();

        abstract Affix removeFirst();

        abstract Affix removeLast();

        abstract Object foldLeft(BiFunction<Object, Object, Object> function, Object initial);

        abstract Object foldRight(BiFunction<Object, Object, Object> function, Object initial);

        abstract int length();
    }

    private static final class One extends Affix {
        final Object sole;

        One(Object value) {
            sole = value;
        }

        @Override
        Affix push(Object value) {
            return new Two(value, measure(value), sole, measure(sole));
        }

        @Override
        Affix inject(Object value) {
            return new Two(sole, measure(sole), value, measure(value));
        }

        @Override
        Object first() {
            return sole;
        }

        @Override
        Object last() {
            return sole;
        }

        @Override
        Affix removeFirst() {
            return null;
        }

        @Override
        Affix removeLast() {
            return null;
        }

        @Override
        Object foldLeft(BiFunction<Object, Object, Object> function, Object initial) {
            return function.apply(initial, sole);
        }

        @Override
        Object foldRight(BiFunction<Object, Object, Object> function, Object initial) {
            return function.apply(initial, sole);
        }

        @Override
        int length() {
            return sole instanceof Node ? ((Node) sole).length() : 1;
        }
    }

    private static abstract class Node extends Affix { }

    private static final class Two extends Node {
        final Object first;
        final Object second;

        final int firstLen;
        final int secondLen;

        Two(Object first, int firstLen, Object second, int secondLen) {
            this.first = first;
            this.firstLen = firstLen;
            this.second = second;
            this.secondLen = secondLen;
        }

        @Override
        Affix push(Object value) {
            return new Three(value, measure(value), first, firstLen, second, secondLen);
        }

        @Override
        Affix inject(Object value) {
            return new Three(first, firstLen, second, secondLen, value, measure(value));
        }

        @Override
        Object first() {
            return first;
        }

        @Override
        Object last() {
            return second;
        }

        @Override
        Affix removeFirst() {
            return new One(second);
        }

        @Override
        Affix removeLast() {
            return new One(first);
        }

        @Override
        Object foldLeft(BiFunction<Object, Object, Object> function, Object initial) {
            Object result = initial;
            result = function.apply(result, first);
            result = function.apply(result, second);
            return result;
        }

        @Override
        Object foldRight(BiFunction<Object, Object, Object> function, Object initial) {
            Object result = initial;
            result = function.apply(result, second);
            result = function.apply(result, first);
            return result;
        }

        @Override
        int length() {
            return firstLen + secondLen;
        }
    }

    private static final class Three extends Node {
        final Object first;
        final Object second;
        final Object third;

        final int firstLen;
        final int secondLen;
        final int thirdLen;

        Three(Object first, int firstLen, Object second, int secondLen, Object third, int thirdLen) {
            this.first = first;
            this.firstLen = firstLen;
            this.second = second;
            this.secondLen = secondLen;
            this.third = third;
            this.thirdLen = thirdLen;
        }

        @Override
        Affix push(Object value) {
            assert false;
            return null;
        }

        @Override
        Affix inject(Object value) {
            assert false;
            return null;
        }

        @Override
        Object first() {
            return first;
        }

        @Override
        Object last() {
            return third;
        }

        @Override
        Affix removeFirst() {
            return new Two(second, secondLen, third, thirdLen);
        }

        @Override
        Affix removeLast() {
            return new Two(first, firstLen, second, secondLen);
        }

        @Override
        Object foldLeft(BiFunction<Object, Object, Object> function, Object initial) {
            Object result = initial;
            result = function.apply(result, first);
            result = function.apply(result, second);
            result = function.apply(result, third);
            return result;
        }

        @Override
        Object foldRight(BiFunction<Object, Object, Object> function, Object initial) {
            Object result = initial;
            result = function.apply(result, third);
            result = function.apply(result, second);
            result = function.apply(result, first);
            return result;
        }

        @Override
        int length() {
            return firstLen + secondLen + thirdLen;
        }
    }
}
