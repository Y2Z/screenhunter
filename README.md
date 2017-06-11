# screenhunter
Automated cursor positioning and clicking tool for X11.

### build
    make clean all

### install
    sudo make install

### uninstall
    sudo make uninstall

### usage
    screenhunter -or button.png
or

    win_id=$(wmctrl -l | grep Firefox | awk '/./{line=$0} END{print $1;}')
    screenhunter -w $win_id button.png

### how it works
Takes one or multiple PNG files on the input, looks for areas matching those images on display and clicks each one of them.

### notes
Please keep in mind that certain applications (mostly web browsers) have a mechanism to prevent such an automation for security reasons.
In such a case the cursor will move and point to the matching area, but the click event will be rejected by the window.

Different programs may render the same image differently (scaling, anti-aliasing, etc).  
Basically you want to make sure to take the screenshot of the desired element using the program you want to automate.

Even if one pixel mismatches between what's on the screen and the target's image, the click won't occur.  
That being said, it's better to use the smallest (yet still unique) part of the element you wish to make your computer to click on.

If the provided target image source contains transparent pixels, screenhunter will treat them as positvie matches.  
This lets the program seek non-rectangular areas on the screen.
