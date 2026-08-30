#!/usr/bin/env escript
%% 10-queens count

main(_) ->
    io:format("~p~n", [solve(10, 1, [])]).

solve(N, Row, _Placed) when Row > N -> 1;
solve(N, Row, Placed) -> try_col(N, Row, Placed, 1).

try_col(N, _Row, _Placed, Col) when Col > N -> 0;
try_col(N, Row, Placed, Col) ->
    With = case safe(Col, Placed, 1) of
        true  -> solve(N, Row + 1, [Col | Placed]);
        false -> 0
    end,
    With + try_col(N, Row, Placed, Col + 1).

safe(_Q, [], _C) -> true;
safe(Q, [X | _], _C) when Q =:= X -> false;
safe(Q, [X | _], C) when abs(Q - X) =:= C -> false;
safe(Q, [_ | Rest], C) -> safe(Q, Rest, C + 1).
