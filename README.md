This is a single header libray for a reliable UDP protocol, meant for games that require minimal latency.

I do not recommend using it yet, it has some unsolved issues and potentially bugs in it. Contributions or tips are welcome as long as they follow the general conventions of this library (single header and in "imperative" coding style).

Ideally I will make it compatiple with C99 after I consider it in an usuable state. Same for usage examples etc.

In windows increasing the timer resolution is highly recommended, because 1ms sleep with the default timer resolution is usually 15-33ms. I'll have to see if there is any more sane way of handling this in Windows. Also you need to link to the neccessary windows libraries (Ws2_32.lib should do).

This software is provided "as is" and the author does not claim any rights or responsibilites for it.
