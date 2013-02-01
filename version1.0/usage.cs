/*
 Define target and texture size.
 Create before any objects that use dynamic texture.
*/
singleton NamedTexTargetObject(MyTexTargetObject) 
{
   targetName = "hellotex";
   texSize = "512";
};

/*
 Map to material reference in object.
*/
singleton Material(testcube_Hello)
{
   mapTo = "Hello";
   diffuseMap[0] = "#hellotex";
};