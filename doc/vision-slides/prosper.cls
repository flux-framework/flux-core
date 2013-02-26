%==============================================================================
% Prosper -- (prosper.cls) Class file
% A LaTeX class for creating slides
% Author: Frederic Goualard (Frederic.Goualard@cwi.nl)
%         Centrum voor Wiskunde en Informatica
%         Amsterdam, The Netherlands
%
% Copyright (c) 2000 Frederic Goualard
% All rights reserved.
% 
% Permission is hereby granted, without written agreement and without
% license or royalty fees, to use, copy, modify, and distribute this
% software and its documentation for any purpose, provided that the
% above copyright notice and the following two paragraphs appear in
% all copies of this software.
% 
% IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, 
% SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF 
% THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHOR HAS BEEN ADVISED 
% OF THE POSSIBILITY OF SUCH DAMAGE.
% 
% THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES,
% INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
% AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
% ON AN "AS IS" BASIS, AND THE AUTHOR HAS NO OBLIGATION TO
% PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
%
% CVSId : $Id: prosper.cls,v 1.1 2004/11/30 20:47:09 garlick1 Exp $
%==============================================================================

\def\Prosper@Version{1.1}
\NeedsTeXFormat{LaTeX2e}[1995/12/01]
\ProvidesClass{prosper}[2001/01/23, v. \Prosper@Version]
\typeout{(c) 2000 Frederic Goualard, CWI, The Netherlands}
\typeout{CVSId: $Id: prosper.cls,v 1.1 2004/11/30 20:47:09 garlick1 Exp $}
\typeout{ }

\newif\ifDVItoPS
\DeclareOption{ps}{\global\DVItoPStrue}
\DeclareOption{pdf}{\global\DVItoPSfalse}

\newif\ifisDraft
\DeclareOption{draft}{\global\isDrafttrue%
  \PassOptionsToClass{draft}{seminar}}
\DeclareOption{final}{\global\isDraftfalse%
  \PassOptionsToClass{final}{seminar}}

\newif\ifinColor
\DeclareOption{slideColor}{\global\inColortrue}
\DeclareOption{slideBW}{\global\inColorfalse}

\newif\ifallPages
\DeclareOption{total}{\global\allPagestrue}
\DeclareOption{nototal}{\global\allPagesfalse}

\newif\ifcolorBG
\DeclareOption{nocolorBG}{\global\colorBGfalse}
\DeclareOption{colorBG}{\global\colorBGtrue}

\newif\ifAccumulate
\DeclareOption{accumulate}{\global\Accumulatetrue}
\DeclareOption{noaccumulate}{\global\Accumulatefalse}

\newif\if@distiller
\@distillerfalse
\DeclareOption{distiller}{\global\@distillertrue}

\DeclareOption{YandY}{\gdef\@pdf@driver{dvipsone}}
\DeclareOption{ps2pdf}{\gdef\@pdf@driver{ps2pdf}}

%% Default style file = default
\def\Style@chosen{default}
\DeclareOption*{\global\let\Style@chosen=\CurrentOption}

\ExecuteOptions{final,slideBW,total,nocolorBG,noaccumulate,ps,ps2pdf}
\ProcessOptions\relax

\LoadClass[portrait,semlayer,semcolor,leqno]{seminar}
\RequirePackage{graphicx}
\RequirePackage[\@pdf@driver,bookmarks,%
                a4paper,%
%                pdfmenubar=false,%
                pdfhighlight=/I,%
%                pdftoolbar=false,%
                pdffitwindow=true,%
                pdfcenterwindow=true,%
%                pdfwindowui=false,%
                menucolor=menucolor,%
%                pdfview=Fit,%
                pdfstartview=Fit]{hyperref}
\RequirePackage{ifthen}

\rotateheaderstrue
\slidewidth=222mm
\slideheight=150mm
\renewcommand{\slideleftmargin}{0.01in}


% Add a bookmark for each master slide
\def\@addBookmarkOnSlide#1{%
  \def\Hy@temp{#1} 
    \pdfmark{pdfmark=/OUT,
      Raw={/Page \thetrueSlideCounter /View [ /XYZ null null null ]
        /Title (\expandafter\strip@prefix\meaning\Hy@temp)}
    }
}

%% Some code to be added when creating a PDF file
\def\Cropping@Box{0 0 595 890} % By default: cropping to a4 paper dimensions
\ifDVItoPS
\else
  \AtEndOfClass{%
    \hypersetup{pdfpagescrop=\Cropping@Box}
  }
\if@distiller
\else
  \AtBeginDvi{%
      \pdfmark{pdfmark=/PAGES,
        Raw={/Rotate 90}}
  }
\fi
\fi
\newcommand{\PDFCroppingBox}[1]{%
  \gdef\Cropping@Box{#1}
  }

%% PDFtransition
%% Transition from one page to another.
%% Possible values:
%%       (Split, Blinds, Box, Wipe, Dissolve, Glitter, R)
\def\Split{} \def\Blinds{} \def\Box{} \def\Wipe{} \def\Dissolve{}
\def\Glitter{} \def\R{} \def\Replace{}
\newcommand{\PDFtransition}[1]{%
  \@ifundefined{#1}
  {%
    \ClassError{prosper}{Undefined transition #1}{%
      Possible values: Split, Blinds, Box, Wipe, Dissolve, Glitter, R}}
  {%
   \hypersetup{pdfpagetransition={#1}}
  }
}

%% Default transition between pages (for values, see \PDFtransition)
\def\@defaultTransition{R}
\newcommand{\DefaultTransition}[1]{\gdef\@defaultTransition{#1}}


\newcommand{\@cartouche}{.}
%% \slideCaption : Definition of caption to appear on every slide.
\newcommand{\slideCaption}[1]{\gdef\@cartouche{#1}}

%%-----------------------------------------------------------------------------
%% Definitions for fonts and colors
%% #1: font and color if slide in color
%% #2: font and color if slide in BW
\def\@fontTitleColor{\ClassError{prosper}{%
    no font specified for title in color}{Use macro \string\FontTitle}}
\def\@fontTitleBW{\ClassError{prosper}{%
    no font specified for title in B\string&W}{Use macro \string\FontTitle}}
\def\@fontSubtitleColor{\@fontTitleColor\fontsize{16pt}{16pt}\selectfont\itshape}
\def\@fontSubtitleBW{\@fontTitleBW\fontsize{14pt}{16pt}\selectfont\itshape}
\def\@fontTextColor{\ClassError{prosper}{%
    no font specified for texte in color}{Use macro \string\FontText}}
\def\@fontTextBW{\ClassError{prosper}{%
    no font specified for text in B\string&W}{Use macro \string\FontText}}
\def\@colorFoot{\black}
\newcommand{\FontTitle}[2]{
  \gdef\@fontTitleColor{#1} 
  \gdef\@fontTitleBW{#2}}
\newcommand{\FontSubtitle}[2]{
  \gdef\@fontSubtitleColor{#1} 
  \gdef\@fontSubtitleBW{#2}}
\newcommand{\FontText}[2]{
  \gdef\@fontTextColor{#1} 
  \gdef\@fontTextBW{#2}}
\newcommand{\ColorFoot}[1]{\gdef\@colorFoot{#1}}

\newcommand{\fontTitle}[1]{%
  {\ifinColor\@fontTitleColor\else\@fontTitleBW\fi #1}}
\newcommand{\fontText}[1]{%
  {\ifinColor\@fontTextColor\else\@fontTextBW\fi #1}}
%%- End of definitions for fonts and colors -----------------------------------

% Width of the text area
\newlength{\slideWidth}

\def\@titleSpacing{\vspace*{10pt}}
\newcommand{\titleSpacing}[1]{\gdef\@titleSpacing{#1}}

%% Redefinition of the slide environment
\newcounter{trueSlideCounter}
\def\thetrueslideCounter{\arabic{trueSlideCounter}}
\let\slide@seminar=\slide
\let\endslide@seminar=\endslide
\let\slide=\relax
\let\endslide=\relax
\newenvironment{SlideNormal}[2][\@defaultTransition]{%
  \StepCounter{trueSlideCounter}
  \PDFtransition{#1}%
  \begin{slide@seminar}
    \ifx#2@
    \else
       \@addBookmarkOnSlide{#2}
       \slidetitle{#2}
    \fi
    \begin{posit@Box}
      \begin{minipage}{\slideWidth}    
        \begin{raggedright}
        \@DefMyItem
        \ifinColor\@fontTextColor\else\@fontTextBW\fi
        }{%
      \end{raggedright}
    \end{minipage}\end{posit@Box}\end{slide@seminar}}
% Slides in overlays
\newenvironment{SlideOverlay}[2][\@defaultTransition]{%
  \ifDVItoPS
      \begin{slide@seminar}
        \slidetitle{#2}
        \begin{posit@Box}
          \begin{minipage}{\slideWidth}
            \begin{raggedright}
            \@DefMyItem
            \ifinColor\@fontTextColor\else\@fontTextBW\fi
  \else %% DVI -> PDF
      \StepCounter{trueSlideCounter}
      \PDFtransition{#1}%
      \begin{slide@seminar}
        \ifx#2@
        \else
          \@ifundefined{@okForBookmark}{%
            \gdef\@okForBookmark{}
            \@addBookmarkOnSlide{#2}
          }{\relax}
          \slidetitle{#2}
        \fi
        \begin{posit@Box}
          \begin{minipage}{\slideWidth} 
            \begin{raggedright}
            \@DefMyItem
            \ifinColor\@fontTextColor\else\@fontTextBW\fi
  \fi    
  }{%
  \ifDVItoPS
        \end{raggedright}
      \end{minipage}\end{posit@Box}\end{slide@seminar}%
  \else %% DVI -> PDF
        \end{raggedright}
      \end{minipage}\end{posit@Box}\end{slide@seminar}
        \addtocounter{slide}{-1}%
  \fi
}
\let\slide=\SlideNormal
\let\endslide=\endSlideNormal

\newcommand{\NewSlideStyle}[4][11cm]{%
  \setlength{\slideWidth}{#1}
  \global\pslongbox{posit@Box}{\rput[#2](#3)}
  \newslideframe{the#4}{\csname #4\endcsname{##1}}
  \slidepagestyle{GenericPageStyle}
  \slideframe{the#4}
}


% Definition of the bullet for itemize env.
\def\@prosperItemi{{\scriptsize\green%
      \raisebox{2pt}{\ensuremath{\bullet}}}}
\def\@prosperItemii{{\scriptsize\green%
      \raisebox{2pt}{\ensuremath{\bullet}}}}
\def\@prosperItemiii{{\scriptsize\green%
      \raisebox{2pt}{\ensuremath{\bullet}}}}
\newcommand{\myitem}[2]{%
  \ifnum#1=1 
    \gdef\@prosperItemi{#2}
  \else
    \ifnum#1=2
       \gdef\@prosperItemii{#2}
    \else
       \ifnum#1=3
          \gdef\@prosperItemiii{#2}
       \else
          \ClassError{prosper}{Undefined item level}{Available levels: 1,2,3}
       \fi
    \fi
  \fi
}

\def\@DefMyItem{%
  \global\let\labelitemi=\@prosperItemi
  \global\let\labelitemii=\@prosperItemii
  \global\let\labelitemiii=\@prosperItemiii
}

%% displaying of a logo (to be defined in every style)
\newcommand{\LogoPosition}[1]{\gdef\@PosLogo{#1}}
\def\@PosLogo{\ClassError{prosper}{no default position for positioning the logo given}{Use macro \string\LogoPosition\space in the style file}}
\gdef\@thelogo{.}
\newcommand{\Logo}{%
    \@ifnextchar({\Logo@one}{\Logo@two}
}
\def\Logo@one(#1)#2{%
  \gdef\@PosLogo{#1}
  \gdef\@thelogo{#2}
}
\def\Logo@two#1{%
  \gdef\@thelogo{#1}
}

\newcommand{\PutLogo}{%
  \if\@thelogo.\else\rput[lb](\@PosLogo){\@thelogo}\fi
}

\expandafter\ifx\csname realpageref\endcsname\relax%
  \gdef\realpageref#1{\expandafter\real@setref
    \csname r@#1\endcsname\@secondoffive{#1}}
\else 
  \relax 
\fi

\newpagestyle{GenericPageStyle}{\hfill }{\hfill%
  \ifisDraft
 {\@colorFoot\tiny \textbf{\jobname.tex}~--~\@Title~--~\@Author~--~\number\day/\number\month/\number\year~--~\timenow~--~p.\thepage\ifallPages/\realpageref{last@page}\fi}
  \else
  \ifshowVersion
 {\@colorFoot\tiny \textbf{\jobname.tex}~--~\@Title~--~\@Author~--~\number\day/\number\month/\number\year~--~\timenow~--~p.\thepage\ifallPages/\realpageref{last@page}\fi}
  \else
  {\@colorFoot\tiny \@cartouche~--~p.\thepage\ifallPages/\realpageref{last@page}\fi}
  \fi\fi
}

\IfFileExists{PPR\Style@chosen.sty}{%
  \RequirePackage{PPR\Style@chosen}
}{\ClassError{prosper}{unknown Prosper style file: PPR\Style@chosen.sty}{%
  Sorry, no additional help}}


\def\nobabel{%
\let\labelitemi\@ltiORI%
\let\labelitemii\@ltiiORI%
\let\labelitemiii\@ltiiiORI%
\let\labelitemiv\@ltivORI}

% A new itemize environment that does not attempt to justify sentences
\let\Itemize=\itemize
\let\endItemize=\enditemize
\renewenvironment{itemize}{\begin{Itemize}\begin{raggedright}}{%
      \end{raggedright}\end{Itemize}}

% itemstep : itemize environment with step-by-step displaying
% Warning: no nesting allowed
\newcounter{item@step}
\newenvironment{itemstep}{%
  \let\old@item=\item
  \begin{itemize}%
    
    \def\item{\StepCounter{item@step}%
      \FromSlide{\theitem@step}\old@item}
    }{\end{itemize}\setcounter{item@step}{0}}

%% Displaying the version caption
\newif\ifshowVersion
\showVersionfalse
\newcommand{\displayVersion}{\global\showVersiontrue}

\newcommand{\timenow}{%
  \@tempcnta=\time \divide\@tempcnta by 60 \number\@tempcnta:\multiply
  \@tempcnta by 60 \@tempcntb=\time \advance\@tempcntb by -\@tempcnta
  \ifnum\@tempcntb <10 0\number\@tempcntb\else\number\@tempcntb\fi}

\newcommand{\lastpage@putlabel}{\addtocounter{page}{-1}%
   \immediate\write\@auxout{\string
   \newlabel{last@page}{{}{\theslide}{\relax }{}{}}}%
   \addtocounter{page}{1}}
\AtEndDocument{\ifallPages\clearpage\lastpage@putlabel\fi}

\newcommand{\@Subtitle}{.}
\newcommand{\subtitle}[1]{\gdef\@Subtitle{#1}}
\renewcommand{\title}[1]{\gdef\@Title{#1}%
  \if\@cartouche.\gdef\@cartouche{#1}\fi}
\renewcommand{\author}[1]{\gdef\@Author{#1}}

\newcommand{\@email}{.}
\newcommand{\email}[1]{\gdef\@email{#1}}

%\newcommand{\@institution}{.}
\newcommand{\institution}[1]{\gdef\@institution{#1}}

\renewcommand{\maketitle}{
  %% Tests whether the 'french' style from B. Gaulle is loaded
  \expandafter\ifx\csname frenchTeXmods\endcsname\relax%
  \else 
  \PackageWarning{prosper}{This page intentionnally left blank to overcome an incompatibility bug between B. Gaulle 'french' package and the seminar class.}
  \begin{center}
    {\black\small
  \textsf{prosper} class: page intentionnally left blank to overcome an incompatibility bug between B. Gaulle 'french' package and the seminar class.}
  \end{center}
  \clearpage \setcounter{page}{0}
  \fi
  \begin{slide}{@}
    \ptsize{10}
    \begin{center}
    \@titleSpacing
    \normalfont
    {\ifinColor\@fontTitleColor\else\@fontTitleBW\fi\@Title}
    \if\@Subtitle.\else

    {\ifinColor\@fontSubtitleColor\else\@fontSubtitleBW\fi\@Subtitle}
    \fi

    \vskip.5em
    \@Author

    \if\@email.\else
    \vskip-5pt
    {\fontsize{7}{7}\selectfont\texttt{\@email}}
    \fi
    \expandafter\ifx\csname @institution\endcsname\relax %
    \else
    \vskip.8em
    \@institution
    \fi
    \vfill
    \end{center}
  \end{slide}
}

%% Overlays environment
\newcounter{overlay@page}
\newenvironment{Overlays}{%
  \global\let\slide\SlideOverlay
  \global\let\endslide\endSlideOverlay
  \setcounter{overlay@page}{\value{slide}}}{%
  \global\let\slide\SlideNormal
  \global\let\endslide\endSlideNormal
  \global\let\@okForBookmark\relax
  \setcounter{slide}{\value{overlay@page}}\StepCounter{slide}}

% overlays environment
% Parameter: # of slides in the overlay
% slide 0 is the one for PostScript file
\newbox\theoverlays
\newcounter{overlaysCount}
\newcounter{limitOverlays}

% fromSlide, onlySlide, untilSlide
% Masks or not the parameter. 
% The stared version does not advance the position pointer and does not
% interpret its argument (to be used for graphics and commands containing
% PStricks links)
\long\def\fromSlide{%
  \@ifnextchar *\fromSlide@E\fromSlide@NE
}
\long\def\fromSlide@NE#1#2{%
  \ifDVItoPS
    \ifAccumulate
      #2
    \fi
  \else
    \ifnum#1>\value{overlaysCount}
      {\overlay{1}#2}
    \else
      #2
    \fi
  \fi
}

\long\def\fromSlide@E*#1#2{%
\ifDVItoPS
    \ifAccumulate
      #2
    \fi
\else
  \ifnum#1>\value{overlaysCount}
    \relax
  \else
    #2
  \fi
\fi
}

\long\def\untilSlide{%
  \@ifnextchar *\untilSlide@E\untilSlide@NE
}
\long\def\untilSlide@NE#1#2{%
\ifDVItoPS
    \ifAccumulate
      #2
    \fi
\else
  \ifnum#1<\value{overlaysCount}
    {\overlay{1}#2}
  \else
    #2
  \fi
\fi
}
\long\def\untilSlide@E*#1#2{%
\ifDVItoPS
    \ifAccumulate
      #2
    \fi
\else
  \ifnum#1<\value{overlaysCount}
    \relax
  \else 
    #2
  \fi
\fi
}

\long\def\onlySlide{%
  \@ifnextchar *\onlySlide@E\onlySlide@NE
}
\long\def\onlySlide@NE#1#2{%
\ifDVItoPS
    \ifAccumulate
      #2
    \fi
\else
  \ifnum#1=\value{overlaysCount}
    #2
  \else
    {\overlay{1}#2}
  \fi
\fi
}
\long\def\onlySlide@E*#1#2{%
\ifDVItoPS
    \ifAccumulate
      #2
    \fi
\else
  \ifnum#1=\value{overlaysCount}
    #2
  \fi
\fi
}


% FromSlide, OnlySlide, UntilSlide
% Masks or not the following material. Advances the position pointer
\newcommand{\FromSlide}[1]{%
\ifDVItoPS
\else
  \ifnum#1>\value{overlaysCount}
    \overlay{1}
  \fi
\fi
}
\newcommand{\UntilSlide}[1]{%
\ifDVItoPS
\else
  \ifnum#1<\value{overlaysCount}
    \overlay{1}
  \fi
\fi
}
\newcommand{\OnlySlide}[1]{%
\ifDVItoPS
\else
  \ifnum#1=\value{overlaysCount}
  \else
    \overlay{1}
  \fi
\fi
}

% Overlays. Parameter to state the number of slides composing the overlay
%\def\stepcounter#1{%
%  \ifInOverlays
%    \ifnum\value{overlayscount}<2
%    \else
%      \addtocounter{#1}\@ne
%    \fi
%  \else
%    \addtocounter{#1}\@ne
%  \fi
%  \begingroup
%    \let\@elt\@stpelt
%    \csname cl@#1\endcsname
%  \endgroup
%}

\newif\ifInOverlays
\InOverlaysfalse
\long\def\overlays#1#2{%
  \global\InOverlaystrue
  \aftergroup\InOverlaysfalse
  \setcounter{limitOverlays}{#1}
  \setcounter{overlaysCount}{1}
  \ifDVItoPS
    #2
  \else
    \begin{Overlays}
    \bgroup  
    \loop 
      #2
    \ifnum\value{overlaysCount}<\value{limitOverlays} 
      \StepCounter{overlaysCount}
    \repeat
    \egroup
    \end{Overlays}
  \fi
}

\def\@listctr{}
\AtBeginDocument{%
  {\begingroup \escapechar\m@ne\xdef\@gtempa{{\string\reset@color}}\endgroup
    \expandafter\@ifundefined\@gtempa\relax{%
      \global\let\orig@reset@color=\reset@color
      \gdef\reset@color{\orig@reset@color\pst@endcolor}}}
 \let\Label=\label
 \def\label#1{%
   \ifInOverlays
     \ifnum\value{overlaysCount}>1
     \else
       \Label{#1}
     \fi
   \fi
   }
 % For amsmath environments
 \let\Label@in@display=\label@in@display
 \def\label@in@display#1{
   \ifInOverlays
     \ifnum\value{overlaysCount}>1
     \else
       \Label@in@display{#1}
     \fi
   \fi
   }

  \let\overlay@loop=\relax
  \let\StepCounter=\stepcounter
  \def\RefStepCounter#1{\StepCounter{#1}%
    \protected@edef\@currentlabel
     {\csname p@#1\endcsname\csname the#1\endcsname}%
  }
  \def\stepcounter#1{%
    \ifInOverlays
      \ifthenelse{\equal{#1}{slide}}{
         \StepCounter{#1}}{
         \ifthenelse{\equal{#1}{\@listctr}}{
            \StepCounter{#1}}{
         \ifnum\value{overlaysCount}>1
         \else
           \StepCounter{#1}
         \fi}}
    \else
      \StepCounter{#1}
    \fi}
% Macro \overlay taken from semlayer.sty
\def\overlay#1{%
  \ifoverlays\ifx\@overlay\relax\nooverlays@err\fi\fi
  \ifslide
    \@tempcnta=#1
    \@testfalse
    \ifnum\@tempcnta>-1 \ifnum\@tempcnta<10 \@testtrue\fi\fi
    \if@test
      \c@overlay=\@tempcnta
      \ifnum\c@overlay=0
        \advance\c@slide by -1
        \RefStepCounter{slide}%
      \else
        \advance\c@overlay by -1
        \RefStepCounter{overlay}%
        \ifoverlays
          \xdef\overlay@list{\overlay@list,\the\c@overlay}%
        \fi
      \fi
      \ifoverlays
        \@overlay{\the\c@overlay}%
      \fi
    \else
      \@seminarerr{\string\overlay\space argument must be
        an integer between 0 and 9}\@eha
    \fi
  \else
    \@seminarerr{\string\overlay\space can only be used
      in slide environments}\@eha
  \fi
  \ignorespaces}

} % AtBeginDocument


% \part
\let\part=\relax
\newcommand{\part}[2][\@defaultTransition]{%
  \begin{slide}[#1]{@}%
    \vspace*{1.5cm}\@addBookmarkOnSlide{#2}
    \begin{center}
      \fontTitle{#2}
    \end{center}
  \end{slide}}

% Uses #1 if PDF option, otherwise #2
\newcommand{\PDForPS}[2]{%
  \ifDVItoPS
    #2
  \else
    #1
  \fi
}

% #1 appears on the slide only on PS mode
\newcommand{\onlyInPS}[1]{\ifDVItoPS #1\fi}
% #1 appears on the slide only on PDF mode
\newcommand{\onlyInPDF}[1]{\ifDVItoPS\relax\else #1\fi}



% To be used in conjunction with babel/french to allow ones own labels
\def\NoFrenchBabelItemize{%
\AtBeginDocument{%
\renewenvironment{itemize}%
{\ifnum \@itemdepth >\thr@@\@toodeep\else
    \advance\@itemdepth\@ne
    \edef\@itemitem{labelitem\romannumeral\the\@itemdepth}%
    \expandafter
    \list
      \csname\@itemitem\endcsname%
    {\let\@trivlist\@trivlistORI
      \settowidth{\labelwidth}{\textendash}%
      \setlength{\leftmargin}{\labelwidth}%
      \addtolength{\leftmargin}{\labelsep}%
      \ifnum\@listdepth=0
      \setlength{\itemindent}{\parindent}%
      \else
      \addtolength{\leftmargin}{\parindent}%
      \fi
      \setlength{\itemsep}{\z@}%
      \setlength{\parsep}{\z@}%
      \setlength{\topsep}{\z@}%
      \setlength{\partopsep}{\z@}%
      \addtolength{\topsep}{-\parskip}%
      \addtolength{\partopsep}{\parskip}%
      }%
    \fi}%
 {\endlist}
}}

\ptsize{14}

\endinput

%%% Local Variables: 
%%% mode: latex
%%% TeX-master: t
%%% End: 
