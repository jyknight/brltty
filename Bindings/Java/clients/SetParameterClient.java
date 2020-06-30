/*
 * libbrlapi - A library providing access to braille terminals for applications.
 *
 * Copyright (C) 2006-2020 by
 *   Samuel Thibault <Samuel.Thibault@ens-lyon.org>
 *   Sébastien Hinderer <Sebastien.Hinderer@ens-lyon.org>
 *
 * libbrlapi comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

package org.a11y.brlapi.clients;
import org.a11y.brlapi.*;

public class SetParameterClient extends Client {
  public SetParameterClient (String... arguments) {
    super(arguments);
    addRequiredParameters("parameter", "value");
  }

  private String parameterName;
  private String parameterValue;

  @Override
  protected void processParameters (String[] parameters)
            throws SyntaxException
  {
    switch (parameters.length) {
      case 0:
        throw new SyntaxException("missing parameter name");

      case 1:
        throw new SyntaxException("missing larameter value");

      case 2:
        parameterName = parameters[0];
        parameterValue = parameters[1];
        return;
    }

    throw new TooManyParametersException(parameters, 2);
  }

  @Override
  protected final void runClient (Connection connection)
            throws OperandException
  {
    Parameter parameter = getParameter(connection, parameterName);
    parameter.set(parameterValue);
  }
}