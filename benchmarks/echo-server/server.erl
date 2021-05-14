-module(server).
-export([listen/1]).
-define(TCP_OPTIONS, [binary, {packet, 0}, {active, false}, {reuseaddr, true}]).

listen(Port) ->
  {ok, LSocket} = gen_tcp:listen(Port, ?TCP_OPTIONS),
  accept(LSocket).
accept(LSocket) ->
  {ok, Socket} = gen_tcp:accept(LSocket),
  spawn(fun() -> loop(Socket) end),
  accept(LSocket).
loop(Socket) ->
  case gen_tcp:recv(Socket, 0) of
    {ok, Data} ->
      gen_tcp:send(Socket, Data),
      gen_tcp:close(Socket);
    {error, closed} ->
      ok
  end.
