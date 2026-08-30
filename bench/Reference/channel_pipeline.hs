import Control.Concurrent (forkIO)
import Control.Concurrent.MVar (newEmptyMVar, putMVar, takeMVar)
import Control.Concurrent.STM.TBQueue
import Control.Monad.STM (atomically)

main :: IO ()
main = do
  ch   <- atomically (newTBQueue 32)
  done <- newEmptyMVar
  _ <- forkIO $ do
    mapM_ (\n -> atomically (writeTBQueue ch (Just (n * 2 :: Int)))) [1..5000]
    atomically (writeTBQueue ch Nothing)
  let loop acc = do
        m <- atomically (readTBQueue ch)
        case m of
          Just v  -> loop (acc + v)
          Nothing -> putMVar done acc
  _ <- forkIO (loop 0)
  r <- takeMVar done
  print r
