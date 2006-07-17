BEGIN{   
    FS="=";
    startfound = 0;
    print "# Copyright (C) 2006  Adam Charrett\n";
    print "# This program is free software; you can redistribute it and/or";
    print "# modify it under the terms of the GNU General Public License";
    print "# as published by the Free Software Foundation; either version 2";
    print "# of the License, or (at your option) any later version.\n";
    
    print "# This program is distributed in the hope that it will be useful,";
    print "# but WITHOUT ANY WARRANTY; without even the implied warranty of";
    print "# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the";
    print "# GNU General Public License for more details.\n";
    
    print "# You should have received a copy of the GNU General Public License";
    print "# along with this program; if not, write to the Free Software";
    print "# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA\n";
    
    print "# msgcodes.py\n";
    
    print "# Binary Communications protocol message codes.\n";
    printf("# (AUTOGENERATED %s)\n", strftime());
    
    print "\n";

    print "#ifndef _MSGCODES_H";
    print "#define _MSGCODES_H\n";
}


/^--CODES-END--/{
    startfound = 0;
    printf("/* End of %s codes */\n\n", codesname);
}

/^0x/{
    if (startfound)
    {
        codename = substr($2, 2, length($2) - 2);
        value    = substr($1, 1, length($1) - 1);
        comment  = substr($3, 2, length($3) - 1);
        printf("#define %s_%s %s /* %s */\n", codesname, codename, value, comment);

    }
}

/^--CODES-START--/{
    startfound = 1;
    codesname = $2;
    printf("/* Start of %s codes */\n", codesname);
}

END{
    print "\n#endif";
}