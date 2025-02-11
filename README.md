Incomplete / TODO

Notes only

> [!WARNING]
> Use `git clone` instead of downloading as a zip, or you will not receive content

> [!NOTE]
> Content requires UE5.5+, however tested and compiling on 5.2 and above.
> <br>Content is not required, provided you can view the content in a 5.5 project

Avoid mesh-based TIP because:
* Fights the sim proxy's mesh smoothing
* Forces you to use inferior linear rotation smoothing
* Forces you to use the inadequate linear translation smoothing that simply isn't good enough because Epic didn't separate them out
* Or, separate them out yourself, which is a lot of work, and the code is one big cobweb that you have to re-learn every time you look at, and adds uncertainty and drastic time for isolation if you have sim proxy issues
* Causes jitter on sim proxies
* Has to be compensated for in every single animation/locomotion system you make
* Causes issues with a lot of procedural systems esp. involving sockets and race conditions with the anim graph's rotate root bone node
* Pervades your anim graph

Use actor-based TIP because:
* Has no issues whatsoever
* Can be tucked away almost entirely in code