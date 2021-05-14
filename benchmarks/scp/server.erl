-module(server).
-export([listen/1]).
-define(TCP_OPTIONS, [binary, {packet, line}, {active, false}, {reuseaddr, true}]).

listen(Port) ->
  {ok, LSocket} = gen_tcp:listen(Port, ?TCP_OPTIONS),
  accept(LSocket).
accept(LSocket) ->
  {ok, Socket} = gen_tcp:accept(LSocket),
  {ok, File} = file:open("result.txt", [write]),
  spawn(fun() -> loop(Socket, File) end),
  accept(LSocket).

loop(Socket, File) ->
  case gen_tcp:recv(Socket, 0) of
    {ok, <<"--over--\n">>} ->
      gen_tcp:close(Socket),
      file:close(File);
    {ok, Data} ->
      io:format(File, "~s", [Data]),
      loop(Socket, File);
    {error, closed} ->
      ok
  end.
