#!/usr/bin/env escript
%% Two producers (1..2500 and 2501..5000) fan in to one consumer; sum.

main(_) ->
    Self = self(),
    Coord = spawn(fun() -> waiter(Self, 0) end),
    spawn(fun() -> producer(Self, Coord, 1, 2500) end),
    spawn(fun() -> producer(Self, Coord, 2501, 5000) end),
    io:format("~p~n", [consume(0)]).

producer(_To, Coord, N, Stop) when N > Stop -> Coord ! done;
producer(To, Coord, N, Stop) ->
    To ! {value, N},
    producer(To, Coord, N + 1, Stop).

waiter(Main, 1) ->
    receive done -> Main ! all_done end;
waiter(Main, Count) ->
    receive done -> waiter(Main, Count + 1) end.

consume(Acc) ->
    receive
        {value, V} -> consume(Acc + V);
        all_done   -> Acc
    after 100 ->
        Acc
    end.
