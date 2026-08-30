import Control.Concurrent (forkIO, threadDelay)
import Control.Concurrent.MVar (MVar, newEmptyMVar, putMVar, takeMVar)

slow :: Int -> IO Int
slow ms = threadDelay (ms * 1000) >> return ms

spawnSlow :: Int -> IO (MVar Int)
spawnSlow ms = do
  out <- newEmptyMVar
  _ <- forkIO (slow ms >>= putMVar out)
  return out

main :: IO ()
main = do
  aM <- spawnSlow 100
  bM <- spawnSlow 100
  cM <- spawnSlow 100
  dM <- spawnSlow 100
  a <- takeMVar aM
  b <- takeMVar bM
  c <- takeMVar cM
  d <- takeMVar dM
  print (a + b + c + d)
