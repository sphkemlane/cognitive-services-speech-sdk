package com.microsoft.cognitiveservices.speech.translation;
//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//


/**
  * Defines translation synthesis result, i.e. the voice output of the translated text in the target language.
  */
public final class TranslationSynthesisResult
{
    // BUG: this is hack for making documentation going.
    TranslationSynthesisResult(com.microsoft.cognitiveservices.speech.internal.TranslationSynthesisResult result)
    {
        _AudioData = new byte[0]; // TODO result.getAudioData();
    }

    /**
      * Translated text in the target language.
      * @return translated text in the target language.
      */
    public byte[] getAudio()
    {
        return _AudioData;
    }// { get; }
    private byte[] _AudioData;

    /**
      * Returns a String that represents the speech recognition result.
      * @return A String that represents the speech recognition result.
      */
    @Override
    public String toString()
    {
        return "AudioData <<" + _AudioData + ">>";
    }
}
