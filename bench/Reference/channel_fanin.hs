import Control.Concurrent (forkIO)
import Control.Concurrent.MVar (newEmptyMVar, putMVar, takeMVar)
import Control.Concurrent.STM.TBQueue
import Control.Monad.STM (atomically)

main :: IO ()
main = do
  work  <- atomically (newTBQueue 32)
  coord <- atomically (newTBQueue 4)
  done  <- newEmptyMVar
  _ <- forkIO $ do
    mapM_ (\n -> atomically (writeTBQueue work (Just (n :: Int)))) [1..2500]
    atomically (writeTBQueue coord ())
  _ <- forkIO $ do
    mapM_ (\n -> atomically (writeTBQueue work (Just (n :: Int)))) [2501..5000]
    atomically (writeTBQueue coord ())
  _ <- forkIO $ do
    atomically (readTBQueue coord)
    atomically (readTBQueue coord)
    atomically (writeTBQueue work Nothing)
  let loop acc = do
        m <- atomically (readTBQueue work)
        case m of
          Just v  -> loop (acc + v)
          Nothing -> putMVar done acc
  _ <- forkIO (loop 0)
  r <- takeMVar done
  print r
