import asyncio


async def slow(x):
    await asyncio.sleep(x / 1000)
    return x


async def main():
    a, b, c, d = await asyncio.gather(
        slow(100), slow(100), slow(100), slow(100)
    )
    print(a + b + c + d)


asyncio.run(main())
