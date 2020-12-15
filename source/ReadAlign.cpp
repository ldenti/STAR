#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "ReadAlign.h"

ReadAlign::ReadAlign(Parameters &Pin, Genome &genomeIn, Transcriptome *TrIn, int iChunk)
    : mapGen(genomeIn), genOut(*genomeIn.genomeOut.g), P(Pin), chunkTr(TrIn)
{
    readNmates = P.readNmates;
    //RNGs
    rngMultOrder.seed(P.runRNGseed * (iChunk + 1));
    rngUniformReal0to1 = std::uniform_real_distribution<double>(0.0, 1.0);
    //transcriptome
    if (P.quant.trSAM.yes)
    {
        alignTrAll = new Transcript[P.alignTranscriptsPerReadNmax];
    };

    if (P.pGe.gType == 101)
    { //SuperTranscriptome
        splGraph = new SpliceGraph(*mapGen.superTr, P, this);
    }
    else
    { //standard map algorithm:
        winBin = new uintWinBin *[2];
        winBin[0] = new uintWinBin[P.winBinN];
        winBin[1] = new uintWinBin[P.winBinN];
        memset(winBin[0], 255, sizeof(winBin[0][0]) * P.winBinN);
        memset(winBin[1], 255, sizeof(winBin[0][0]) * P.winBinN);
        //split
        splitR = new uint *[3];
        splitR[0] = new uint[P.maxNsplit];
        splitR[1] = new uint[P.maxNsplit];
        splitR[2] = new uint[P.maxNsplit];
        //alignments
        PC = new uiPC[P.seedPerReadNmax];
        WC = new uiWC[P.alignWindowsPerReadNmax];
        nWA = new uint[P.alignWindowsPerReadNmax];
        nWAP = new uint[P.alignWindowsPerReadNmax];
        WALrec = new uint[P.alignWindowsPerReadNmax];
        WlastAnchor = new uint[P.alignWindowsPerReadNmax];

        WA = new uiWA *[P.alignWindowsPerReadNmax];
        for (uint ii = 0; ii < P.alignWindowsPerReadNmax; ii++)
            WA[ii] = new uiWA[P.seedPerWindowNmax];
        WAincl = new bool[P.seedPerWindowNmax];

#ifdef COMPILE_FOR_LONG_READS
        swWinCov = new uint[P.alignWindowsPerReadNmax];
        scoreSeedToSeed = new intScore[P.seedPerWindowNmax * (P.seedPerWindowNmax + 1) / 2];
        scoreSeedBest = new intScore[P.seedPerWindowNmax];
        scoreSeedBestInd = new uint[P.seedPerWindowNmax];
        scoreSeedBestMM = new uint[P.seedPerWindowNmax];
        seedChain = new uint[P.seedPerWindowNmax];
#endif
    };

    //aligns a.k.a. transcripts
    trAll = new Transcript **[P.alignWindowsPerReadNmax + 1];
    nWinTr = new uint[P.alignWindowsPerReadNmax];
    trArray = new Transcript[P.alignTranscriptsPerReadNmax];
    trArrayPointer = new Transcript *[P.alignTranscriptsPerReadNmax];
    for (uint ii = 0; ii < P.alignTranscriptsPerReadNmax; ii++)
        trArrayPointer[ii] = &(trArray[ii]);
    trInit = new Transcript;

    if (mapGen.genomeOut.convYes)
    { //allocate output transcripts
        trMultOut = new Transcript *[P.outFilterMultimapNmax];
        for (uint32 ii = 0; ii < P.outFilterMultimapNmax; ii++)
            trMultOut[ii] = new Transcript;
    };

    //read
    Read0 = new char *[2];
    Read0[0] = new char[DEF_readSeqLengthMax + 1];
    Read0[1] = new char[DEF_readSeqLengthMax + 1];
    Qual0 = new char *[2];
    Qual0[0] = new char[DEF_readSeqLengthMax + 1];
    Qual0[1] = new char[DEF_readSeqLengthMax + 1];
    readNameMates = new char *[P.readNmates];
    for (uint ii = 0; ii < P.readNmates; ii++)
    {
        readNameMates[ii] = new char[DEF_readNameLengthMax];
    };
    readNameExtra.resize(P.readNmates);
    readName = readNameMates[0];
    Read1 = new char *[3];
    Read1[0] = new char[DEF_readSeqLengthMax + 1];
    Read1[1] = new char[DEF_readSeqLengthMax + 1];
    Read1[2] = new char[DEF_readSeqLengthMax + 1];
    Qual1 = new char *[2]; //modified QSs for scoring
    Qual1[0] = new char[DEF_readSeqLengthMax + 1];
    Qual1[1] = new char[DEF_readSeqLengthMax + 1];

    //outBAM
    outBAMoneAlignNbytes = new uint[P.readNmates + 2]; //extra piece for chimeric reads
    outBAMoneAlign = new char *[P.readNmates + 2];     //extra piece for chimeric reads
    for (uint ii = 0; ii < P.readNmates + 2; ii++)
    {
        outBAMoneAlign[ii] = new char[BAMoutput_oneAlignMaxBytes];
    };
    resetN();

    //chim
    chunkOutChimJunction = new fstream;
    chimDet = new ChimericDetection(P, trAll, nWinTr, Read1, mapGen, chunkOutChimJunction, this);

    //solo
    soloRead = new SoloRead(P, iChunk);
};

void ReadAlign::resetN()
{ //reset resets the counters to 0 for a new read
    mapMarker = 0;
    nA = 0;
    nP = 0;
    nW = 0;
    nTr = 0;
    nTrMate = 0;
    nUM[0] = 0;
    nUM[1] = 0;
    storedLmin = 0;
    uniqLmax = 0;
    uniqLmaxInd = 0;
    multLmax = 0;
    multLmaxN = 0;
    multNminL = 0;
    multNmin = 0;
    multNmax = 0;
    multNmaxL = 0;
    chimN = 0;

    for (uint ii = 0; ii < P.readNmates; ii++)
    {
        maxScoreMate[ii] = 0;
    };
};

ReadAlign::~ReadAlign()
{
    //transcriptome
    if (P.quant.trSAM.yes)
    {
        delete[] alignTrAll;
    };

    if (P.pGe.gType == 101)
    { //SuperTranscriptome
        delete splGraph;
    }
    else
    { //standard map algorithm:
        delete[] winBin[0];
        delete[] winBin[1];
        delete[] winBin;

        //split
        delete[] splitR[0];
        delete[] splitR[1];
        delete[] splitR[2];
        delete[] splitR;
        //alignments
        delete[] PC;
        delete[] WC;
        delete[] nWA;
        delete[] nWAP;
        delete[] WALrec;
        delete[] WlastAnchor;

        for (uint ii = 0; ii < P.alignWindowsPerReadNmax; ii++)
            delete[] WA[ii];
        delete[] WA;

        delete[] WAincl;
    };

    //aligns a.k.a. transcripts
    delete[] trAll;
    delete[] nWinTr;
    delete[] trArray;
    // for (uint ii = 0; ii < P.alignTranscriptsPerReadNmax; ii++)
    // {
    //     delete[] trArrayPointer[ii];
    //     delete[] trArray[ii];
    // }
    delete[] trArrayPointer;
    delete trInit;

    // if (mapGen.genomeOut.convYes)
    // { //allocate output transcripts
    //     trMultOut = new Transcript *[P.outFilterMultimapNmax];
    //     for (uint32 ii = 0; ii < P.outFilterMultimapNmax; ii++)
    //         trMultOut[ii] = new Transcript;
    // };

    //read
    delete[] Read0[0];
    delete[] Read0[1];
    delete[] Read0;
    delete[] Qual0[0];
    delete[] Qual0[1];
    delete[] Qual0;
    for (uint ii = 0; ii < P.readNmates; ii++)
        delete[] readNameMates[ii];
    delete[] readNameMates;

    delete[] Read1[0];
    delete[] Read1[1];
    delete[] Read1[2];
    delete[] Read1;
    delete[] Qual1[0];
    delete[] Qual1[1];
    delete[] Qual1;

    //outBAM
    delete[] outBAMoneAlignNbytes;
    for (uint ii = 0; ii < P.readNmates + 2; ii++)
        delete[] outBAMoneAlign[ii];
    delete[] outBAMoneAlign;

    //chim
    delete chunkOutChimJunction;
    delete chimDet;

    //solo
    // delete soloRead;
};