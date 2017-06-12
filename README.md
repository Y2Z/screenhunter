# screenhunter
Automated cursor positioning and clicking tool for X11.

### build
    make clean all

### install
    sudo make install

### uninstall
    sudo make uninstall

### usage
    Usage: screenhunter [options] path/to/target1.png [path/to/target2.png [...]]

      -s          scan only, do not perform any clicks
      -k          do not return the cursor to its original position
      -r          randomize delays and coordinates
      -o          exit after the first match
      -c <count>  set the amount of clicks done per matching area
      -w <ID>     target a specific X11 window by its ID
      -v          output version information and exit
      -h          display this help message and exit

### example

    win_id=$(wmctrl -l | grep Firefox | awk '/./{line=$0} END{print $1;}')
    screenhunter -w $win_id -os button.png
    if [ $? -eq 0 ]
    then
        wmctrl -a -i $win_id
        screenhunter -w $win_id -or button.png
    fi

### purpose
`screenhunter` can be used to add hotkeys to almost any program.
It can as well be helpful in acceptance testing, automating web browsing,
setting up alert systems, scripting screen capture videos, and many more.

### how it works
It takes one or multiple PNG files on the input, looks for areas matching those
images on display and clicks each one of them.

### notes
Please keep in mind that certain applications (mostly web browsers) have a mechanism to prevent such an automation for security reasons.
In such a case the cursor will move and point to the matching area, but the click event will be rejected by the window.

Different programs may render the same image differently (font face, scaling, anti-aliasing, etc).  
Basically you want to make sure to take the screenshot of the desired element using the program you want to automate.

Even if one pixel mismatches between what's on the screen and the target's image, the click won't occur.  
That being said, it's better to use the smallest (yet still unique) part of the element you wish to make your computer to click on.

If the provided target image source contains transparent pixels, screenhunter will treat them as positvie matches.  
This lets the program seek non-rectangular areas on the screen.
