# screenhunter
Automated cursor positioning and clicking tool for X11.

### building
`make clean all`

### installation
`sudo make install`

### how it works
Takes one or multiple PNG files on the input, seeks for spots matching those images on the screen and clicks on every matching one.

### usage
`./screenhunter button.png [icon.png [...]]`

### notes
Please keep in mind that certain applications (mostly web browsers) have a mechanism to prevent such automation for security reasons.
In such a case the mouse will move and point to the matching area, but the click event will be rejected by the window.

Different programs may render the same image differently (scaling, anti-aliasing, etc).  
Basically you want to make sure to take the screenshot of the desired element using the program you want to automate.

Even if one pixel mismatches between what's on the screen and the target's image, screenhunter the click won't occur.  
Therefore it's better to use the smallest yet unique part of the element you wish to make your computer click on.

### uninstallation
`sudo make uninstall`
