import Control.Concurrent (threadDelay)
slow :: Int -> IO Int
slow ms = threadDelay (ms * 1000) >> return ms
main :: IO ()
main = do
  a <- slow 100
  b <- slow a
  c <- slow b
  d <- slow c
  print d
